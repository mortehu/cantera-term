#include <errno.h>
#include <stdlib.h>
#include <stdio.h>

#include <err.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/uio.h>
#include <sysexits.h>
#include <unistd.h>

int
call(int fd, const char *name)
{
  struct iovec io;
  struct msghdr header;
  struct cmsghdr *cheader;
  ssize_t ret;
  int pipes[3][2];
  int remote_fds[3];
  char cmsgbuf[CMSG_SPACE (sizeof (remote_fds))];

  if (-1 == pipe(pipes[0]))
    return -1;

  if (-1 == pipe(pipes[1]))
    {
      close (pipes[0][0]);
      close (pipes[0][1]);

      return -1;
    }

  if (-1 == pipe(pipes[2]))
    {
      close (pipes[0][0]);
      close (pipes[0][1]);
      close (pipes[1][0]);
      close (pipes[1][1]);

      return -1;
    }

  remote_fds[0] = pipes[0][0];
  remote_fds[1] = pipes[1][1];
  remote_fds[2] = pipes[2][1];

  memset(&io, 0, sizeof(io));
  io.iov_base = (void *) name;
  io.iov_len = strlen(name);

  memset(&header, 0, sizeof(header));
  header.msg_iov = &io;
  header.msg_iovlen = 1;
  header.msg_control = cmsgbuf;
  header.msg_controllen = sizeof(cmsgbuf);

  cheader = CMSG_FIRSTHDR(&header);
  cheader->cmsg_level = SOL_SOCKET;
  cheader->cmsg_type = SCM_RIGHTS;
  cheader->cmsg_len = CMSG_LEN(sizeof(remote_fds));

  memcpy(CMSG_DATA(cheader), remote_fds, sizeof(remote_fds));

  ret = sendmsg(fd, &header, 0);

  close (remote_fds[0]);
  close (remote_fds[1]);
  close (remote_fds[2]);

  if (ret == -1)
    {
      close (pipes[0][1]);
      close (pipes[1][0]);
      close (pipes[2][0]);

      return -1;
    }

  for (;;)
    {
      char buf[4096];

      ret = read (pipes[1][0], buf, sizeof(buf));

      if (ret <= 0)
        break;

      write(1, buf, ret);
    }

  close (pipes[0][1]);
  close (pipes[1][0]);
  close (pipes[2][0]);

  return 0;
}

int
main (int argc, char **argv)
{
  struct sockaddr_un addr;
  int unix_fd;

  if (-1 == (unix_fd = socket (AF_UNIX, SOCK_STREAM, 0)))
    err (EX_OSERR, "Cannot create Unix stream socket");

  memset (&addr, 0, sizeof (addr));
  addr.sun_family = AF_UNIX;
  sprintf (addr.sun_path + 1, "cantera-job-daemon-%u", (unsigned int) getuid ());

  if (-1 == connect (unix_fd, (struct sockaddr *) &addr, sizeof (addr)))
    err (EX_OSERR, "Failed to connect to Unix socket");

  call (unix_fd, "ghostbusters");

  return EXIT_SUCCESS;
}
