#include <abt.h>
#include <time.h>

#define N_CORE (8)
#define ULT_N_TH (8*16)

static ABT_xstream abt_xstreams[N_CORE];
static ABT_thread abt_threads[ULT_N_TH];
static ABT_pool global_abt_pools[N_CORE];

typedef struct {
  int tid;
  volatile int *quit;
} arg_t;

void
func(void *p)
{
  arg_t *arg = (arg_t *)p;
  int tid = arg->tid;


  while (1) {
    if (arg->quit) {
      break;
    }


    
    ABT_thread_yield();
  }
  
}

int
main()
{
  int i;
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
  for (tid=0; tid<ULT_N_TH; tid++) {
    args[tid].tid = tid;
    args[tid].quit = &quit;
    int ret = ABT_thread_create(global_abt_pools[tid % N_CORE],
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
  
  for (tid=0; tid<ULT_N_TH; tid++) {
    ABT_thread_join(abt_threads[tid]);
  }
  
}
