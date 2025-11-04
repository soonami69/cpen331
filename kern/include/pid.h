#ifndef _PID_H_
#define _PID_H_

#include <types.h>
#include <synch.h>
#include <limits.h>

typedef int pid_t;

struct proc;

struct pid_entry {
    pid_t pid;
    struct proc* proc; /* null if process has terminated */
    bool exited;
    int exitcode; /* status passed to wait */
    int refcount; /* number of references to this entry */
    struct lock *pe_lock;
    struct cv *pe_cv;
};

void pid_bootstrap(void); /* initizalize pid related stuff */

struct pid_entry *pid_alloc(struct proc *p); /* allocate a pid entry and attach the proc to it. Returns a pointer/null */

void pid_hold(struct pid_entry *pe); /* increment refcount */
void pid_release(struct pid_entry *pe); /* decrement refcount, free when 0 */
void pid_set_exit(struct pid_entry *pe, int exitcode); /* call this when a process exits */
int pid_wait(struct pid_entry* pe, int* status);

struct pid_entry *pid_lookup(pid_t pid); /* get the pid_entry associated with this pid */

#endif