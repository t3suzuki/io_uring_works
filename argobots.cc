#include <abt.h>
#include <time.h>
#include <liburing.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <assert.h>
#include <pthread.h>

#define TIME_SEC (10)

#define N_CORE (1)
#define N_ULT_PER_CORE (512)
#define ULT_N_TH (N_CORE*N_ULT_PER_CORE)
#define IO_URING_QD (N_ULT_PER_CORE*8)
#define IO_URING_TH1 (512)
#define IO_URING_TH2 (0)

static struct io_uring ring[N_CORE][128];
static ABT_xstream abt_xstreams[N_CORE];
static ABT_thread abt_threads[ULT_N_TH];
static ABT_pool global_abt_pools[N_CORE];
static int done_flag[N_CORE][IO_URING_QD];
static int file_fd;
static int pending_req[N_CORE][128];
static int submit_cnt[N_CORE][128];

typedef struct {
  int tid;
  int core_id;
  volatile int *quit;
  size_t count;
} arg_t;

int done = 0;

static inline
void __io_uring_check(int core_id)
{
  struct io_uring_cqe *cqe;
  unsigned head;
  int i = 0;
#if 1
  io_uring_for_each_cqe(&ring[core_id][0], head, cqe) {
    if (cqe->res > 0) {
      //printf("%s %d\n", __func__, __LINE__);
      done_flag[core_id][cqe->user_data] = 1;
      i++;
    }
  }
  if (i > 0)
    io_uring_cq_advance(&ring[core_id][0], i);
#else
  {
    struct io_uring *r = (struct io_uring *)(ring[core_id]);
    struct io_uring_cq *cring = &r->cq;
	struct io_uring_cqe *cqe;
	unsigned head, reaped = 0;
	int last_idx = -1, stat_nr = 0;

	head = *cring->khead;
	do {

		if (head == *cring->ktail)
		  break;
		//printf("%s %d\n", __func__, __LINE__);
		cqe = &cring->cqes[head & *cring->kring_mask];
		{
		  int index = cqe->user_data & 0xffffffff;

		  if (cqe->res > 0) {
		    //printf("res %d index %d head %d\n", cqe->res, index, head);
		  } else {
		    assert(0);
		  }
		  done_flag[0][index] = 1;
		}
		reaped++;
		head++;
	} while (1);

	if (reaped) {
	  *cring->khead = head;
	}
  }
#endif
}

#include <fcntl.h>


static inline
void __io_uring_bottom(int core_id, int sqe_id)
{
#if 1
  *ring[core_id][0].sq.ktail = ring[core_id][0].sq.sqe_tail;
  syscall(__NR_io_uring_enter, ring[core_id][0].enter_ring_fd, 1, 0, 0, NULL, 0);

  //io_uring_submit(&ring[core_id][0]);
#else
  int sub_cnt = -1;
  if (pending_req[core_id][0] >= IO_URING_TH1) {
    //printf("io_uring_submit %d\n", core_id);
    io_uring_submit(&ring[core_id][0]);
    pending_req[core_id][0] = 0;
    submit_cnt[core_id][0] = (submit_cnt[core_id][0] + 1) % 65536;
  } else {
    pending_req[core_id][0]++;
    sub_cnt = submit_cnt[core_id][0];
    int i = 0;
    while (1) {
      if (sub_cnt != submit_cnt[core_id][0]) {
	break;
      }
      if (i > IO_URING_TH2) {
	//printf("io_uring_submit2 %d pend %d submitted %d\n", core_id, pending_req[core_id][0], submit_cnt[core_id][0]);
	io_uring_submit(&ring[core_id][0]);
	pending_req[core_id][0] = 0;
	submit_cnt[core_id][0] = (submit_cnt[core_id][0] + 1) % 65536;
	break;
      }
      ABT_thread_yield();
      i++;
    }
  }
#endif
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
  int ret = posix_memalign(&buf, sz, sz);
  size_t count = 0;
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
    sqe->user_data = sqe_id;
    done_flag[ring_id][sqe_id] = 0;
    __io_uring_bottom(ring_id, sqe_id);
#else
    size_t unit = 32768;
    size_t off;
    for (off=0; off<sz; off+=unit) {
      struct io_uring_sqe *sqe = io_uring_get_sqe(&ring[core_id][0]);
      assert(sqe);
      io_uring_prep_write(sqe, file_fd, (char *)buf+off, unit, pos+off);
      int sqe_id = ((uint64_t)sqe - (uint64_t)ring[core_id][0].sq.sqes) / sizeof(struct io_uring_sqe);
      sqe->user_data = sqe_id;
      done_flag[core_id][sqe_id] = 0;
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
    //printf("%s %d\n", __func__, __LINE__);
    struct io_uring_sqe *sqe = io_uring_get_sqe(&ring[core_id][0]);
    int sqe_id = ((uint64_t)sqe - (uint64_t)ring[core_id][0].sq.sqes) / sizeof(struct io_uring_sqe);
#if 1
    io_uring_prep_read(sqe, file_fd, buf, sz, pos);
#else
    io_uring_prep_read_fixed(sqe, file_fd, buf, sz, pos, sqe_id);
#endif
    sqe->user_data = sqe_id;
    done_flag[core_id][sqe_id] = 0;
    __io_uring_bottom(core_id, sqe_id);
    count ++;
    done ++;
  }
  arg->count = count;
}

void
run(void *arg)
{
  int i;
  volatile int *quit = (volatile int *)arg;
    
  ABT_init(0, NULL);
  ABT_xstream_self(&abt_xstreams[0]);
  for (i=1; i<N_CORE; i++) {
    ABT_xstream_create(ABT_SCHED_NULL, &abt_xstreams[i]);
  }
  for (i=0; i<N_CORE; i++) {
    ABT_xstream_set_cpubind(abt_xstreams[i], i);
    ABT_xstream_get_main_pools(abt_xstreams[i], 1, &global_abt_pools[i]);
  }

  ABT_thread abt_threads[ULT_N_TH];
  int tid;
  arg_t args[ULT_N_TH];

  struct timespec t0;
  clock_gettime(CLOCK_MONOTONIC, &t0);

  for (tid=0; tid<ULT_N_TH; tid++) {
    int core_id = tid % N_CORE;
    args[tid].tid = tid;
    args[tid].core_id = core_id;
    args[tid].quit = quit;
    int ret = ABT_thread_create(global_abt_pools[core_id],
				(void (*)(void*))func,
				&args[tid],
				ABT_THREAD_ATTR_NULL,
				&abt_threads[tid]);
    
  }

  ABT_thread wth;
  arg_t warg;
  int use_wfunc = 0;
  warg.count = 0;
  if (use_wfunc) {
    int core_id = 0;
    warg.quit = quit;
    warg.core_id = core_id;
    ABT_thread_create(global_abt_pools[core_id],
		      (void (*)(void*))wfunc,
		      &warg,
		      ABT_THREAD_ATTR_NULL,
		      &wth);
  }


  size_t sum = 0;
  for (tid=0; tid<ULT_N_TH; tid++) {
    ABT_thread_join(abt_threads[tid]);
    sum += args[tid].count;
  }
  if (use_wfunc)
    ABT_thread_join(wth);
  
  struct timespec t2;
  clock_gettime(CLOCK_MONOTONIC, &t2);

  double d_sec = (t2.tv_sec - t0.tv_sec) + (t2.tv_nsec - t0.tv_nsec) * 1e-9;

  printf("%f %lu %f KIOPS\n", d_sec, sum, sum / d_sec / 1000.0);
  printf("write count %lu\n", warg.count);
  
}


int
main(int argc, char **argv)
{
  int i;
  /*
  struct rlimit rlim;
  rlim.rlim_cur = RLIM_INFINITY;
  rlim.rlim_max = RLIM_INFINITY;
  setrlimit(RLIMIT_MEMLOCK, &rlim);
  */
  
  for (i=0; i<N_CORE; i++) {
    io_uring_queue_init(IO_URING_QD, &ring[i][0], 0);
    //printf("%s %d\n", __func__, __LINE__);
    //io_uring_queue_init(IO_URING_QD, &ring[i][0], IORING_SETUP_SQPOLL);
    /*
    return syscall(__NR_io_uring_register, s->ring_fd,
		   IORING_REGISTER_BUFFERS, s->iovecs, roundup_pow2(depth));
    */
  }
  char *file_path = argv[1];
  file_fd = open(file_path, O_RDWR|O_DIRECT);
  assert(file_fd > 0);
  printf("Opened file: %s\n", file_path);

  volatile int quit = 0;
  pthread_t pth;
  pthread_create(&pth, NULL,  (void* (*)(void*))run, (void*)&quit);

  int prev_done = 0;
  for (i=0; i<TIME_SEC; i++) {
    sleep(1);
    printf("%d sec %d KIOPS\n", i, (done - prev_done)/ 1000);
    prev_done = done;
  }

  quit = 1;
  pthread_join(pth, NULL);
  
}
