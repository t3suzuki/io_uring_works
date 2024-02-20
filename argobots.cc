#include <abt.h>

#define N_CORE (8)
#define ULT_N_TH (8*16)

static ABT_xstream abt_xstreams[N_CORE];
static ABT_thread abt_threads[ULT_N_TH];
static ABT_pool global_abt_pools[N_CORE];

void
func(void *arg)
{
  int tid = *(int *)arg;
  
  printf("s %d\n", tid);
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

  ABT_thread abt_threads[ULT_N_TH];
  int tid;
  int args[ULT_N_TH];
  for (tid=0; tid<ULT_N_TH; tid++) {
    args[tid] = tid;
    int ret = ABT_thread_create(global_abt_pools[tid % N_CORE],
				(void (*)(void*))func,
				&args[tid],
				ABT_THREAD_ATTR_NULL,
				&abt_threads[tid]);
    
  }
  for (tid=0; tid<ULT_N_TH; tid++) {
    ABT_thread_join(abt_threads[tid]);
  }
  
}
