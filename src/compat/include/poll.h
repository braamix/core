// <poll.h>. The constants; `poll` names its b_poll (compat/cio.h).
#pragma once

#include <sys/cdefs.h>
#include <sys/types.h>

// Linux's numbers, so a port's own tables of them still mean what they say.
#define POLLIN   0x001
#define POLLPRI  0x002
#define POLLOUT  0x004
#define POLLERR  0x008
#define POLLHUP  0x010
#define POLLNVAL 0x020

typedef unsigned long nfds_t;

struct pollfd {
    int fd;
    short events;
    short revents;
};

#ifndef BRAAM_COMPAT_BUILDING

int poll(struct pollfd *fds, nfds_t n, int timeout)
    BRAAM_BLOCKS("b_poll(fds, n, timeout)");

#endif
