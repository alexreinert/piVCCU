#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <poll.h>

int main(int argc, char *argv[])
{
  if (argc != 2)
  {
    printf("Usage: %s <path>\n", argv[0]);
    return -1;
  }

  int fd = open(argv[1], O_RDONLY);
  if (fd < 0)
  {
    close(fd);
    printf("%s could not be opened\n", argv[1]);
    return -1;
  }

  char buffer[1024];
  if (read(fd, buffer, sizeof(buffer)) == -1)
  {
    printf("%s could is not readable\n", argv[1]);
    return -1;
  }

  struct pollfd fds = { .fd = fd, .events = POLLPRI|POLLERR, .revents = 0 };

  if (poll(&fds, 1, -1) <= 0)
  {
    return -1;
  }

  if (lseek(fd, 0, SEEK_SET) == -1)
  {
    return -1;
  }

  int cnt = read(fd, buffer, sizeof(buffer) - 1);
  if (cnt <= 0)
  {
    return -1;
  }

  close(fd);

  buffer[cnt] = 0;
  printf("%s", buffer);

  return 0;
}

