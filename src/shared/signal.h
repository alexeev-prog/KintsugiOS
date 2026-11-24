#ifndef KOS_SHARED_SIGNAL_H
#define KOS_SHARED_SIGNAL_H

/* Signals are cooperative in KintsugiOS: a pending signal is delivered at the
 * next syscall/tick, like SIGNALED in a classic microkernel. There are no
 * userspace handlers yet. */
#define SIGHUP  1
#define SIGINT  2
#define SIGQUIT 3
#define SIGKILL 9
#define SIGUSR1 10
#define SIGTERM 15
#define SIGUSR2 12
#define SIGCHLD 17
#define SIGCONT 18
#define SIGSTOP 19

#define SIG_EXIT_INTERRUPTED 130
#define SIG_EXIT_KILLED 137

#endif
