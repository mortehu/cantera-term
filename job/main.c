#include <errno.h>
#include <stdlib.h>
#include <stdio.h>

#include <err.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/inotify.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/uio.h>
#include <sysexits.h>
#include <unistd.h>

#include "tree.h"

#define EPOLL_MAX_EVENTS 32
#define MAX_PENDING_JOBS 128

struct job
{
  char *function_name;
  int input_fd;
  int output_fd;
  int error_fd;
};

static struct job jobs[MAX_PENDING_JOBS];
static unsigned int jobs_write, jobs_read;
static pthread_mutex_t jobs_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t jobs_not_full = PTHREAD_COND_INITIALIZER;
static pthread_cond_t jobs_not_empty = PTHREAD_COND_INITIALIZER;
static pthread_t job_thread;

static struct tree* config;
static pthread_mutex_t config_lock = PTHREAD_MUTEX_INITIALIZER;

static char *
get_string (int fd)
{
  char *result;
  size_t alloc, size;
  ssize_t ret;

  alloc = 4096;
  result = malloc (alloc);

  while (0 < (ret = read (fd, result + size, alloc - size)))
    {
      size += ret;

      if (size == alloc)
        {
          alloc += 4096;
        }
    }
}
static void
process_call (const char *name, int input_fd, int output_fd, int error_fd)
{
  FILE *err;
  pid_t child;

  if (!strncmp (name, "cantera-", 8))
    {
    }
  if (-1 == (child = fork ()))
    {
      err = fdopen (error_fd, "w");

      fprintf (err, "fork failed: %s", strerror (errno));

      fclose (err);

      return;
    }

  if (child)
    return;

  /*
  write (output_fd, "Hest", 4);
  */
  exit (EXIT_FAILURE);
}

static void *
process_calls (void *arg)
{
  char *function_name;
  unsigned int i;
  int input_fd;
  int output_fd;
  int error_fd;

  for (;;)
    {
      pthread_mutex_lock (&jobs_lock);

      while (jobs_read == jobs_write)
        pthread_cond_wait (&jobs_not_empty, &jobs_lock);

      i = jobs_read++;
      i %= MAX_PENDING_JOBS;
      function_name = jobs[i].function_name;
      input_fd = jobs[i].input_fd;
      output_fd = jobs[i].output_fd;
      error_fd = jobs[i].error_fd;

      pthread_cond_signal (&jobs_not_full);

      pthread_mutex_unlock (&jobs_lock);

      process_call (function_name, input_fd, output_fd, error_fd);

      free (function_name);

      close (input_fd);
      close (output_fd);
      close (error_fd);
    }

  return 0;
}

static void
add_call (const char *name, int input_fd, int output_fd, int error_fd)
{
  unsigned int i;
  char *name_copy;

  name_copy = strdup (name);

  pthread_mutex_lock (&jobs_lock);

  while (jobs_write == jobs_read + MAX_PENDING_JOBS)
    pthread_cond_wait (&jobs_not_full, &jobs_lock);

  i = jobs_write++;
  i %= MAX_PENDING_JOBS;
  jobs[i].function_name = name_copy;
  jobs[i].input_fd = input_fd;
  jobs[i].output_fd = output_fd;
  jobs[i].error_fd = error_fd;

  pthread_cond_signal (&jobs_not_empty);

  pthread_mutex_unlock (&jobs_lock);
}

static void
handle_inotify (int inotify_fd)
{
  struct inotify_event* ev;
  char* buf;
  size_t size;
  int result, available = 0;

  result = ioctl(inotify_fd, FIONREAD, &available);

  if(available > 0)
    {
      buf = malloc(available);

      result = read(inotify_fd, buf, available);

      /* EINVAL should not be possible here, since epoll told us we had at
       * least on event before doing ioctl(FIONREAD) */

      if(result < 0)
        err (EX_OSERR, "Read error from inotify socket");

      if(result < sizeof(struct inotify_event))
        errx (EX_OSERR, "Short read from inotify socket");

      ev = (struct inotify_event*) buf;

      while(available)
        {
          size = sizeof(struct inotify_event) + ev->len;

          if(size > available)
            errx (EX_OSERR, "Corrupt data in inotify event stream");

          if(!strcmp(ev->name, "config")
             && (ev->mask & (IN_CLOSE_WRITE | IN_MOVED_TO)))
            {
              pthread_mutex_lock (&config_lock);

              tree_destroy(config);
              config = tree_load_cfg(".cantera/config");

              pthread_mutex_unlock (&config_lock);
            }

          available -= size;
          ev = (struct inotify_event*) ((char*) ev + size);
        }

      free(buf);
    }
}

int
main (int argc, char **argv)
{
  struct epoll_event evs[32];
  struct sockaddr_un addr;
  const char *home;
  int inotify_fd;
  int unix_fd;
  int epoll_fd;
  int ret, i;

  if (geteuid () != getuid ())
    errx (EX_USAGE, "Effective user ID must be equal to user ID");

  if (0 == (home = getenv ("HOME")))
    errx (EX_USAGE, "No HOME environment variable set");

  if (-1 == chdir (home))
    err (EX_OSERR, "Failed to change directory to '%s'", home);

  signal (SIGPIPE, SIG_IGN);
  signal (SIGALRM, SIG_IGN);

  if (-1 == (inotify_fd = inotify_init1 (IN_CLOEXEC)))
    err (EX_OSERR, "Failed to initialize inotify");

  if (-1 == inotify_add_watch (inotify_fd, ".cantera", IN_ALL_EVENTS | IN_ONLYDIR))
    err (EX_OSERR, "Failed to start watching '.cantera' using intofy");

  if (0 != (ret = pthread_create (&job_thread, 0, process_calls, 0)))
    errx (EX_OSERR, "Faild to create job thread (pthread error code = %d)", ret);

  if (-1 == (epoll_fd = epoll_create (EPOLL_MAX_EVENTS)))
    err (EX_OSERR, "Failed to create epoll instance (epoll_create)");

  if (-1 == (unix_fd = socket (AF_UNIX, SOCK_STREAM, 0)))
    err (EX_OSERR, "Cannot create Unix stream socket");

  memset (&addr, 0, sizeof (addr));
  addr.sun_family = AF_UNIX;
  sprintf (addr.sun_path + 1, "cantera-job-daemon-%u", (unsigned int) getuid ());

  if (-1 == bind (unix_fd, (struct sockaddr *) &addr, sizeof (addr)))
    {
      if (errno == EADDRINUSE)
        return EXIT_SUCCESS;

      err (EX_OSERR, "Failed to bind Unix socket");
    }

  if (-1 == listen (unix_fd, 16))
    err (EX_OSERR, "Failed to start listening on Unix socket");

  memset (&evs[0], 0, sizeof (evs[0]));
  evs[0].data.fd = unix_fd;
  evs[0].events = EPOLLIN;

  if (-1 == epoll_ctl (epoll_fd, EPOLL_CTL_ADD, unix_fd, &evs[0]))
    err (EX_OSERR, "Failed to add Unix stream socket to epoll instance");

  if (-1 == epoll_ctl (inotify_fd, EPOLL_CTL_ADD, unix_fd, &evs[0]))
    err (EX_OSERR, "Failed to add inotify stream socket to epoll instance");

  for (;;)
    {
      ret = epoll_wait (epoll_fd, evs, EPOLL_MAX_EVENTS, -1);

      if (ret == -1)
        {
          if (ret == EINTR)
            continue;

          err (EX_OSERR, "Error while waiting for I/O event from epoll instance");
        }

      for (i = 0; i < ret; ++i)
        {
          if (evs[i].data.fd == unix_fd)
            {
              int peer_fd;

              peer_fd = accept (unix_fd, 0, 0);

              memset (&evs[0], 0, sizeof (evs[0]));
              evs[0].data.fd = peer_fd;
              evs[0].events = EPOLLIN;

              if (-1 == epoll_ctl (epoll_fd, EPOLL_CTL_ADD, peer_fd, &evs[0]))
                err (EX_OSERR, "Failed to add peer socket to epoll instance");
            }
          else if (evs[i].data.fd == inotify_fd)
            {
              handle_inotify (inotify_fd);
            }
          else
            {
              char name[4097];
              struct iovec io;
              struct msghdr header;
              struct cmsghdr *cheader;
              ssize_t ret;
              int fds[3];
              char cmsgbuf[CMSG_SPACE (sizeof (fds))];

              memset (&io, 0, sizeof (io));
              io.iov_base = (void *) name;
              io.iov_len = sizeof (name) - 1;

              memset (&header, 0, sizeof (header));
              header.msg_iov = &io;
              header.msg_iovlen = 1;
              header.msg_control = cmsgbuf;
              header.msg_controllen = sizeof (cmsgbuf);

              if (0 >= (ret = recvmsg (evs[i].data.fd, &header, 0)))
                {
                  if (ret == -1)
                    fprintf (stderr, "recvmsg failed: %s\n", strerror (errno));

                  close (evs[i].data.fd);

                  continue;
                }

              for (cheader = CMSG_FIRSTHDR (&header); cheader; cheader = CMSG_NXTHDR (&header, cheader))
                {
                  if (cheader->cmsg_level == SOL_SOCKET
                      && cheader->cmsg_type == SCM_RIGHTS)
                    {
                      memcpy (fds, CMSG_DATA (cheader), sizeof (fds));

                      break;
                    }
                }

              if (!cheader)
                {
                  fprintf (stderr, "Missing file descriptors in message\n");

                  close (evs[i].data.fd);

                  continue;
                }

              if (!header.msg_iovlen || !header.msg_iov->iov_len)
                {
                  fprintf (stderr, "Missing function name in message\n");

                  close (evs[i].data.fd);

                  continue;
                }

              name[header.msg_iov->iov_len] = 0;

              add_call (name, fds[0], fds[1], fds[2]);
            }
        }
    }

  return EXIT_SUCCESS;
}
