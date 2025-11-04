#include <kern/errno.h>
#include <pid.h>
#include <types.h>
#include <limits.h>
#include <lib.h>

#define PID_TABLE_SIZE PID_MAX

static struct pid_entry *pid_table[PID_MAX];
static struct lock *pid_table_lock; /* to prevent multiple threads from touching the table at the same time */
static struct cv* pid_cv;              /* CV to signal PID release */

void pid_bootstrap(void) {
    int i;
    pid_table_lock = lock_create("pid table lock");
    pid_cv = cv_create("pid_cv");
    KASSERT(pid_cv != NULL);

    for (i = 0; i < PID_TABLE_SIZE; i++) {
        pid_table[i] = NULL;
    }
}

static struct pid_entry *pid_entry_create(pid_t pid, struct proc *p) {
    struct pid_entry *pe = kmalloc(sizeof(*pe));

    if (!pe) {
        return NULL;
    }

    pe->pid = pid;
    pe->proc = p;
    pe->exited = false;
    pe->exitcode = 0;
    pe->refcount = 1;
    pe->pe_lock = lock_create("pid_lock");
    pe->pe_cv = cv_create("pid_cv");

    if (!pe->pe_lock || !pe->pe_cv) {
        if (pe->pe_lock)
            lock_destroy(pe->pe_lock);
        if (pe->pe_cv)
            cv_destroy(pe->pe_cv);
        kfree(pe);
        return NULL;
    }
    return pe;
}

/* find a free pid, create a pid_entry then return it */
struct pid_entry* pid_alloc(struct proc* p) {
    KASSERT(p != NULL);

    struct pid_entry* pe = NULL;

    lock_acquire(pid_table_lock);

    while (pe == NULL) {
        /* Scan for a free PID */
        for (int idx = PID_MIN; idx < PID_TABLE_SIZE; idx++) {
            if (pid_table[idx] == NULL) {
                pe = pid_entry_create(idx, p);
                if (!pe) {
                    lock_release(pid_table_lock);
                    return NULL;  /* memory allocation failure */
                }
                pid_table[idx] = pe;
                lock_release(pid_table_lock);
                return pe; 
            }
        }

        /* No free PID found: sleep on CV until a PID is released */
        cv_wait(pid_cv, pid_table_lock);
        /* When woken up, loop will retry scanning table */
    }

    /* should never reach here */
    lock_release(pid_table_lock);
    return pe;
}

/* pid_hold: increment reference (caller must ensure pe != NULL) */
void pid_hold(struct pid_entry* pe) {
    KASSERT(pe != NULL);
    lock_acquire(pe->pe_lock);
    pe->refcount++;
    lock_release(pe->pe_lock);
}

/* pid_release: decrement refcount and free if 0 */
void pid_release(struct pid_entry* pe) {

    if (!pe) {
        return;
    }

    bool should_free = false;
    lock_acquire(pe->pe_lock);
    pe->refcount--;
    if (pe->refcount <= 0) {
        should_free = true;
    }
    lock_release(pe->pe_lock);

    if (should_free) {
        /* remove from table */
        lock_acquire(pid_table_lock);
        if (pid_table[pe->pid] == pe) {
            pid_table[pe->pid] = NULL;
        }
        lock_release(pid_table_lock);

        /* cleanup */
        lock_destroy(pe->pe_lock);
        cv_destroy(pe->pe_cv);
        kfree(pe);
    }
}

struct pid_entry *pid_lookup(pid_t pid) {
    struct pid_entry *pe = NULL;

    if (pid < PID_MIN || pid >= PID_TABLE_SIZE) {
        return NULL;
    }

    lock_acquire(pid_table_lock);
    pe = pid_table[pid];
    if (pe != NULL) {
        lock_acquire(pe->pe_lock);
        pe->refcount++;
        lock_release(pe->pe_lock);
    }
    lock_release(pid_table_lock);
    return pe; /* the caller now has one reference to pe */
}

/* mark the pid entry as exited and set exitcode; wake waiters */
void pid_set_exit(struct pid_entry* pe, int exitcode) {
    KASSERT(pe != NULL);
    lock_acquire(pe->pe_lock);
    pe->exited = true;
    pe->exitcode = exitcode;
    /* drop pointer to struct proc now that it's dead */
    pe->proc = NULL;
    /* wake any waiters */
    cv_broadcast(pe->pe_cv, pe->pe_lock);
    /* release the process's own reference: the exiting process should have decreased its own refcount elsewhere */
    lock_release(pe->pe_lock);
}

int pid_wait(struct pid_entry* pe, int* status) {
    if (!pe)
        return ESRCH;
    lock_acquire(pe->pe_lock);
    while (!pe->exited) {
        cv_wait(pe->pe_cv, pe->pe_lock);
    }
    if (status)
        *status = pe->exitcode;
    lock_release(pe->pe_lock);
    /* caller must release the reference obtained earlier */
    return 0;
}