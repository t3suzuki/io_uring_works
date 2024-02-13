#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <liburing.h>
#include <stdlib.h>
#include <assert.h>

#define QUEUE_DEPTH (128)
#define N_IO (3)
#define BUF_SIZE (4096)

int
main()
{
  int ret;
  char *buf[N_IO];
  int i, j;
  struct io_uring ring;

  // init io_uring
  ret = io_uring_queue_init(QUEUE_DEPTH, &ring, 0);

  // alloc buf
  for (i=0; i<N_IO; i++) {
    ret = posix_memalign((void **)&buf[i], BUF_SIZE, BUF_SIZE);
  }
  
}
