#include <abt.h>
#include <time.h>
#include <liburing.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <assert.h>

#define TIME_SEC (10)

#define N_CORE (2)
#define N_ULT_PER_CORE (16)
#define ULT_N_TH (N_CORE*N_ULT_PER_CORE)
#define IO_URING_QD (N_ULT_PER_CORE*16)

static struct io_uring ring[N_CORE+1][128];
static ABT_xstream abt_xstreams[N_CORE+1];
static ABT_thread abt_threads[ULT_N_TH];
static ABT_pool global_abt_pools[N_CORE+1];
static int done_flag[N_CORE+1][IO_URING_QD];
static int file_fd;

typedef struct {
  int tid;
  int core_id;
  volatile int *quit;
  size_t count;
} arg_t;


static inline
void __io_uring_check(int core_id)
{
  struct io_uring_cqe *cqe;
  unsigned head;
  int i = 0;
  io_uring_for_each_cqe(&ring[core_id][0], head, cqe) {
    if (cqe->res > 0) {
      //printf("%s %d\n", __func__, __LINE__);
      done_flag[core_id][cqe->user_data] = 1;
      i++;
    }
  }
  if (i > 0)
    io_uring_cq_advance(&ring[core_id][0], i);
}

static inline
void __io_uring_bottom(int core_id, int sqe_id)
{
  io_uring_submit(&ring[core_id][0]);
  while (1) {
    __io_uring_check(core_id);
    if (done_flag[core_id][sqe_id])
      break;
    ABT_thread_yield();
  }
}

void
wfunc(void *p)
{
  arg_t *arg = (arg_t *)p;
  int core_id = arg->core_id;
  void *buf;
  const size_t sz = 2*1024*1024;
  //const size_t sz = 128*1024;
  //const size_t sz = 4*1024;
  //printf("%s %d\n", __func__, __LINE__);
  int ret = posix_memalign(&buf, sz, sz);
  size_t count = 0;
  //printf("%s %d %d\n", __func__, __LINE__, ret);
  while (1) {
    if (*arg->quit) {
      break;
    }
    size_t pos = (sz * count) % (1ULL*1024*1024*1024);
#if 0
    int ring_id = N_CORE;
    struct io_uring_sqe *sqe = io_uring_get_sqe(&ring[ring_id][0]);
    assert(sqe);
    io_uring_prep_write(sqe, file_fd, buf, sz, pos);
    int sqe_id = ((uint64_t)sqe - (uint64_t)ring[ring_id][0].sq.sqes) / sizeof(struct io_uring_sqe);
    //sqe->flags = 16; //#define REQ_F_FORCE_ASYNC
    sqe->user_data = sqe_id;
    done_flag[ring_id][sqe_id] = 0;
    //printf("%s %d %lu %d %lu\n", __func__, __LINE__, count, sqe_id, pos);
    __io_uring_bottom(ring_id, sqe_id);
#else
    size_t unit = 32768;
    size_t off;
    for (off=0; off<sz; off+=unit) {
      struct io_uring_sqe *sqe = io_uring_get_sqe(&ring[core_id][0]);
      assert(sqe);
      io_uring_prep_write(sqe, file_fd, (char *)buf+off, unit, pos+off);
      int sqe_id = ((uint64_t)sqe - (uint64_t)ring[core_id][0].sq.sqes) / sizeof(struct io_uring_sqe);
      //sqe->flags = 16; //#define REQ_F_FORCE_ASYNC
      sqe->user_data = sqe_id;
      done_flag[core_id][sqe_id] = 0;
    //printf("%s %d %lu %d %lu\n", __func__, __LINE__, count, sqe_id, pos);
      __io_uring_bottom(core_id, sqe_id);
    }
#endif
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
    struct io_uring_sqe *sqe = io_uring_get_sqe(&ring[core_id][0]);
    io_uring_prep_read(sqe, file_fd, buf, sz, pos);
    int sqe_id = ((uint64_t)sqe - (uint64_t)ring[core_id][0].sq.sqes) / sizeof(struct io_uring_sqe);
    sqe->user_data = sqe_id;
    done_flag[core_id][sqe_id] = 0;
    __io_uring_bottom(core_id, sqe_id);
    count ++;
  }
  arg->count = count;
}

int
main(int argc, char **argv)
{
  int i;
  for (i=0; i<N_CORE+1; i++) {
    io_uring_queue_init(IO_URING_QD, &ring[i][0], 0);
  }
  char *file_path = argv[1];
  file_fd = open(file_path, O_RDWR|O_DIRECT);
  assert(file_fd > 0);
  printf("Opened file: %s\n", file_path);

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
