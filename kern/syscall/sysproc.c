#include <types.h>
#include <current.h>
#include <fdtable.h>
#include <kern/errno.h>
#include <lib.h>
#include <mips/trapframe.h>
#include <pid.h>
#include <proc.h>
#include <syscall.h>
#include <addrspace.h>
#include <copyinout.h>


int sys_getpid(pid_t *retval) {
    if (retval == NULL) {
        return EFAULT;
    }

    KASSERT(curproc != NULL);
    KASSERT(curproc->p_pidentry != NULL);

    *retval = curproc->p_pidentry->pid;
    return 0;
}

int sys_waitpid(pid_t pid, userptr_t status, int options, pid_t* retval) {
    if (options != 0){
        return EINVAL;
    }

    if (pid < PID_MIN || pid > PID_MAX){
        return ESRCH;
    }

    struct pid_entry *pe = pid_lookup(pid);

    int waitcode;
    pid_wait(pe, &waitcode);

    pid_release(pe);

    if (status != NULL){
        int result = copyout(&waitcode, (userptr_t)status, sizeof(int32_t));
        if (result) {
            return result;
        }
    }

    *retval = pid;

    return 0;
}

int sys_fork(struct trapframe* tf, pid_t* retval) {
    struct proc* child;
    int result;

    if (!tf || !retval) {
        return EFAULT;
    }

    /* Create new child process */
    child = proc_create_runprogram(curproc->p_name);
    if (!child) {
        return ENPROC;
    }

    /* Copy file descriptor table */
    if (curproc->p_fdtable) {
        child->p_fdtable = fdtable_clone(curproc->p_fdtable);
        if (!child->p_fdtable) {
            proc_destroy(child);
            return ENOMEM;
        }
    }

    /* Copy current working directory */
    if (curproc->p_cwd) {
        VOP_INCREF(curproc->p_cwd);
        child->p_cwd = curproc->p_cwd;
    }

    /* Allocate PID entry for child */
    struct pid_entry* pe = pid_alloc(child);
    if (!pe) {
        proc_destroy(child);
        return ENPROC;
    }
    child->p_pidentry = pe;

    /* Allocate and copy trapframe */
    struct trapframe* child_tf = kmalloc(sizeof(struct trapframe));
    if (!child_tf) {
        pid_release(pe);
        proc_destroy(child);
        return ENOMEM;
    }
    memcpy(child_tf, tf, sizeof(struct trapframe));

    /* Copy parent's address space */
    result = as_copy(curproc->p_addrspace, &child->p_addrspace);
    if (result) {
        kfree(child_tf);
        pid_release(pe);
        proc_destroy(child);
        return result;
    }

    /*
     * Spawn child thread
     * The trapframe will be freed by enter_forked_process.
     */
    result = thread_fork(curthread->t_name,
                         child,
                         enter_forked_process,
                         child_tf,
                         0);
    if (result) {
        kfree(child_tf);
        pid_release(pe);
        proc_destroy(child);
        return result;
    }

    /* Parent returns child's PID */
    *retval = pe->pid;

    return 0;
}
