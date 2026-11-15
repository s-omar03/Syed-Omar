/*###############################################################################################
 *
 *           Author         :
 *           Date           : 10-10-2026
 *           File           : signal_pipe.c
 *           Descriptions   : Self-pipe signal handling: the handler only writes the signal
 *                            number into a non-blocking pipe, the main loop reads it back
 *           Objective      : Handle termination signals in normal program context, where it
 *                            is safe to log, send packets and free memory
 *           Usage          : Used by server.c and client.c through signal_pipe.h
 *           Output         : None (library module)
 *
###############################################################################################*/

#define _POSIX_C_SOURCE 200809L     /* sigaction() */

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include "signal_pipe.h"

#define PIPE_READ_END    0
#define PIPE_WRITE_END   1
#define DRAIN_CHUNK      16

static int pipe_write_fd = -1;  /* Written only before the handlers are installed */

/*
 * Function    : on_signal
 * Description : Signal handler. write() is async-signal-safe; errno is
 *               preserved for the interrupted code.
 */
static void on_signal(int sig)
{
    int saved = errno;
    unsigned char byte = (unsigned char)sig;
    ssize_t rc = write(pipe_write_fd, &byte, 1);

    (void)rc;                   /* A full pipe already holds a pending signal */
    errno = saved;
}

/*
 * Function    : set_pipe_flags
 * Description : Makes a pipe end non-blocking and close-on-exec
 * Return      : 0 on success, -1 on error
 */
static int set_pipe_flags(int fd)
{
    int flags = fcntl(fd, F_GETFL);

    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0)
        return -1;
    return fcntl(fd, F_SETFD, FD_CLOEXEC);
}

/*
 * Function    : signal_pipe_init
 * Description : Ignores SIGPIPE, creates the pipe and installs the handler
 *               for every signal in 'signals'. SA_RESTART keeps blocking
 *               calls in other threads running; poll() still returns.
 * Return      : Read end of the pipe, or -1 on error
 */
int signal_pipe_init(const int *signals, size_t count)
{
    struct sigaction sa;
    int fds[2];
    size_t i;

    memset(&sa, 0, sizeof sa);
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGPIPE, &sa, NULL) != 0)
        return -1;

    if (pipe(fds) != 0)
        return -1;
    if (set_pipe_flags(fds[PIPE_READ_END]) != 0 || set_pipe_flags(fds[PIPE_WRITE_END]) != 0)
    {
        close(fds[PIPE_READ_END]);
        close(fds[PIPE_WRITE_END]);
        return -1;
    }
    pipe_write_fd = fds[PIPE_WRITE_END];

    sa.sa_handler = on_signal;
    sa.sa_flags = SA_RESTART;
    for (i = 0; i < count; i++)
    {
        if (sigaction(signals[i], &sa, NULL) != 0)
            return -1;
    }
    return fds[PIPE_READ_END];
}

/*
 * Function    : signal_pipe_drain
 * Description : Reads every pending byte from the pipe
 * Return      : Number of the last signal received, or 0 if none
 */
int signal_pipe_drain(int fd)
{
    unsigned char buf[DRAIN_CHUNK];
    int last = 0;

    for (;;)
    {
        ssize_t n = read(fd, buf, sizeof buf);

        if (n > 0)
        {
            last = buf[n - 1];
            continue;
        }
        if (n < 0 && errno == EINTR)
            continue;
        break;
    }
    return last;
}
