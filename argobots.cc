#include <abt.h>
#include <time.h>
#include <liburing.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <assert.h>

#define N_CORE (2)
#define N_ULT_PER_CORE (4)
#define ULT_N_TH (N_CORE*N_ULT_PER_CORE)
#define IO_URING_QD (N_ULT_PER_CORE*16)

static struct io_uring ring[N_CORE][128];
static ABT_xstream abt_xstreams[N_CORE];
static ABT_thread abt_threads[ULT_N_TH];
static ABT_pool global_abt_pools[N_CORE];
static int done_flag[N_CORE][IO_URING_QD];
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
func(void *p)
{
  arg_t *arg = (arg_t *)p;
  int tid = arg->tid;
  int core_id = arg->core_id;
  void *buf;
  size_t count = 0;
  posix_memalign(&buf, 4096, 4096);
  while (1) {
    if (*arg->quit) {
      break;
    }
    //printf("%s %d %d\n", __func__, __LINE__, tid);

    const size_t sz = 4096;
    size_t pos = (rand() % (1024 * 256)) * 4096ULL;
    //size_t pos = 16;
    struct io_uring_sqe *sqe = io_uring_get_sqe(&ring[core_id][0]);
    io_uring_prep_read(sqe, file_fd, buf, sz, pos);
    int sqe_id = ((uint64_t)sqe - (uint64_t)ring[core_id][0].sq.sqes) / sizeof(struct io_uring_sqe);
    sqe->user_data = sqe_id;
    done_flag[core_id][sqe_id] = 0;

    //printf("%s %d %d\n", __func__, __LINE__, tid);
    __io_uring_bottom(core_id, sqe_id);
    //printf("%s %d %d\n", __func__, __LINE__, tid);
    count ++;
  }
  arg->count = count;
}

int
main(int argc, char **argv)
{
  int i;
  for (i=0; i<N_CORE; i++) {
    io_uring_queue_init(IO_URING_QD, &ring[i][0], 0);
  }
  char *file_path = argv[1];
  file_fd = open(file_path, O_RDONLY|O_DIRECT);
  assert(file_fd > 0);
  printf("Opened file: %s\n", file_path);

  ABT_init(0, NULL);
  ABT_xstream_self(&abt_xstreams[0]);
  for (i=1; i<N_CORE; i++) {
    ABT_xstream_create(ABT_SCHED_NULL, &abt_xstreams[i]);
  }
  for (i=0; i<N_CORE; i++) {
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

  struct timespec t1;
  clock_gettime(CLOCK_MONOTONIC, &t1);
  while (1) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    double diff_sec = (now.tv_sec - t1.tv_sec) + (now.tv_nsec - t1.tv_nsec) * 1e-9;
    if (diff_sec > 5)
      break;
    ABT_thread_yield();
  }
  
  quit = 1;

  size_t sum = 0;
  for (tid=0; tid<ULT_N_TH; tid++) {
    ABT_thread_join(abt_threads[tid]);
    sum += args[tid].count;
  }

  struct timespec t2;
  clock_gettime(CLOCK_MONOTONIC, &t2);

  double d_sec = (t2.tv_sec - t0.tv_sec) + (t2.tv_nsec - t0.tv_nsec) * 1e-9;

  printf("%f %lu %f KIOPS\n", d_sec, sum, sum / d_sec / 1000.0);
}
