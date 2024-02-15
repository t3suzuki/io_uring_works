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
#define LEN (512)

int
main(int argc, char **argv)
{
  int ret;
  char *buf[N_IO];
  int i, j;
  struct io_uring ring;
  
  assert(argc == 2);

  // ---- init io_uring ----
  ret = io_uring_queue_init(QUEUE_DEPTH, &ring, 0);

  // ---- alloc buf ----
  for (i=0; i<N_IO; i++) {
    ret = posix_memalign((void **)&buf[i], BUF_SIZE, BUF_SIZE);
  }

  // ---- file open ----
  char *file_path = argv[1];
  int file_fd = open(file_path, O_RDONLY|O_DIRECT);
  assert(file_fd > 0);
  printf("Opened file: %s\n", file_path);

  // ---- prepare read requests ----
  struct iovec iovec[N_IO];
  for (i=0; i<N_IO; i++) {
    struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
    iovec[i].iov_base = buf[i];
    iovec[i].iov_len = LEN;
    int nr_vecs = 1;
    int offset = i * BUF_SIZE;
#if 1
    io_uring_prep_readv(sqe, file_fd, &iovec[i], nr_vecs, offset);
#else
    ret = preadv(file_fd, &iovec[i], nr_vecs, offset);
#endif
  }

  // ---- submit read requests ----
  ret = io_uring_submit(&ring);

  // ---- check completions ----
  struct io_uring_cqe *cqe;
  for (i=0; i<N_IO; i++) {
    while (1) {
      ret = io_uring_peek_cqe(&ring, &cqe);
      if (ret == 0) {
	break;
      }
    }
    if (cqe->res < 0) {
      printf("[fail] cqe->res: %d\n", cqe->res);
      assert(0);
    }
    assert(cqe->res == LEN);
  }
  
}
