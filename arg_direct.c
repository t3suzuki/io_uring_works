#include <abt.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <assert.h>

#include "nvme.h"

#define TIME_SEC (10)

#define N_CORE (2)
#define N_ULT_PER_CORE (16)
#define ULT_N_TH (N_CORE*N_ULT_PER_CORE)

static ABT_xstream abt_xstreams[N_CORE+1];
static ABT_thread abt_threads[ULT_N_TH];
static ABT_pool global_abt_pools[N_CORE+1];

typedef struct {
  int tid;
  int core_id;
  volatile int *quit;
  size_t count;
} arg_t;

void
wfunc(void *p)
{
  arg_t *arg = (arg_t *)p;
  int core_id = arg->core_id;
  const int qd = 512;
  void *buf[qd];
  int rid[qd];
  const size_t sz = 128*1024*1024;
  int i;
  for (i=0; i<qd; i++) {
    posix_memalign(&buf[i], sz, sz);
  }
  size_t count = 0;
  while (1) {
    if (*arg->quit) {
      break;
    }
    size_t pos = (sz * count) % (1ULL*1024*1024*1024);
    for (i=0; i<qd; i++) {
      rid[i] = nvme_write_req(pos / 512 + 8 * i, 8, core_id, (int)4096, (char *)buf[i]);
    }
    for (i=0; i<qd; i++) {
      while (1) {
	if (nvme_check(rid[i]))
	  break;
	ABT_thread_yield();
      }
    }
    count ++;
  }
  arg->count = count;  
}

void
func(void *p)
{
  arg_t *arg = (arg_t *)p;
  int tid = arg->tid;
  int core_id = arg->core_id;
  void *buf;
  size_t count = 0;
  const size_t sz = 4096;
  int ret = posix_memalign(&buf, sz, sz);
  //printf("%s %d %d\n", __func__, __LINE__, ret);
  while (1) {
    if (*arg->quit) {
      break;
    }

    size_t pos = (rand() % (1024 * 256)) * 4096ULL;

    int rid = nvme_read_req(pos / 512, sz / 512, core_id, sz, buf);
    while (1) {
      if (nvme_check(rid))
	break;
      ABT_thread_yield();
    }
    
    count ++;
  }
  arg->count = count;
}

int
main(int argc, char **argv)
{
  int i;
  nvme_init();
  
  ABT_init(0, NULL);
  ABT_xstream_self(&abt_xstreams[0]);
  for (i=1; i<N_CORE+1; i++) {
    ABT_xstream_create(ABT_SCHED_NULL, &abt_xstreams[i]);
  }
  for (i=0; i<N_CORE+1; i++) {
    ABT_xstream_set_cpubind(abt_xstreams[i], i);
    ABT_xstream_get_main_pools(abt_xstreams[i], 1, &global_abt_pools[i]);
  }

  volatile int quit = 0;
  ABT_thread abt_threads[ULT_N_TH];
  int tid;
  arg_t args[ULT_N_TH];

  struct timespec t0;
  clock_gettime(CLOCK_MONOTONIC, &t0);

  for (tid=0; tid<ULT_N_TH; tid++) {
    int core_id = tid % N_CORE;
    args[tid].tid = tid;
    args[tid].core_id = core_id;
    args[tid].quit = &quit;
    int ret = ABT_thread_create(global_abt_pools[core_id],
				(void (*)(void*))func,
				&args[tid],
				ABT_THREAD_ATTR_NULL,
				&abt_threads[tid]);
    
  }

  ABT_thread wth;
  arg_t warg;
  {
    int core_id = N_CORE;
    warg.quit = &quit;
    warg.core_id = core_id;
    ABT_thread_create(global_abt_pools[core_id],
		      (void (*)(void*))wfunc,
		      &warg,
		      ABT_THREAD_ATTR_NULL,
		      &wth);
  }
  struct timespec t1;
  clock_gettime(CLOCK_MONOTONIC, &t1);
  while (1) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    double diff_sec = (now.tv_sec - t1.tv_sec) + (now.tv_nsec - t1.tv_nsec) * 1e-9;
    if (diff_sec > TIME_SEC)
      break;
    ABT_thread_yield();
  }
  
  quit = 1;

  size_t sum = 0;
  for (tid=0; tid<ULT_N_TH; tid++) {
    ABT_thread_join(abt_threads[tid]);
    sum += args[tid].count;
  }
  printf("%s %d\n", __func__, __LINE__);
  ABT_thread_join(wth);
  
  struct timespec t2;
  clock_gettime(CLOCK_MONOTONIC, &t2);

  double d_sec = (t2.tv_sec - t0.tv_sec) + (t2.tv_nsec - t0.tv_nsec) * 1e-9;

  printf("%f %lu %f KIOPS\n", d_sec, sum, sum / d_sec / 1000.0);
  printf("write count %lu\n", warg.count);
}
