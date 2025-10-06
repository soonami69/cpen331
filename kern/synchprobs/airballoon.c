/*
 * Driver code for airballoon problem
 */
#include <types.h>
#include <lib.h>
#include <synch.h>
#include <thread.h>
#include <test.h>

#define N_LORD_FLOWERKILLER 8
#define NROPES 16
static int ropes_left = NROPES;

/* Data structures for rope mappings */
int rope_to_stake[NROPES]; // which rope is connected to stake n?
int stake_to_rope[NROPES];
bool rope_cut[NROPES];

/* Implement this! */

/* Synchronization primitives */
struct lock* rope_locks[NROPES];
struct lock* count_lock; // to update the counter
struct lock* print_lock; // to ensure printing is atomic
struct cv* all_ropes_gone;  // to signal balloon

/* Implement this! */

// Structures for main to keep track of running threads
static int threads_remaining;
static struct lock* threads_lock;
static struct cv* threads_done_cv;

/*
 * Design:
 * - Each rope has a per-rope lock; threads must acquire it before cutting or modifying.
 * - Marigold picks a random stake, Dandelion a random hook (rope ID = hook index).
 * - FlowerKiller swaps two stakes; acquires both rope locks in increasing rope ID order to avoid deadlock.
 * - ropes_left is protected by count_lock; Balloon waits on all_ropes_gone CV.
 * - threads_remaining tracks live threads; main waits on threads_done_cv before cleanup.
 *
 * Invariants:
 * - A rope is cut at most once.
 * - Rope mappings and rope_cut are only modified under rope locks.
 *
 * Exit:
 * - Worker threads exit when ropes_left == 0.
 * - Balloon exits when ropes_left == 0.
 * - Main exits after all threads signal completion.
 */

// helper function to remove a thread from threads_remaining
static void thread_exit_notify(void) {
    lock_acquire(threads_lock);
    threads_remaining--;
    if (threads_remaining == 0) {
        cv_signal(threads_done_cv, threads_lock);
    }
    lock_release(threads_lock);
}

static void
dandelion(void* p, unsigned long arg) {
    (void)p;
    (void)arg;

    lock_acquire(print_lock);
    kprintf("Dandelion thread starting\n");
    lock_release(print_lock);

    while (true) {
        lock_acquire(count_lock);
        if (ropes_left == 0) {
            cv_broadcast(all_ropes_gone, count_lock);
            lock_release(count_lock);
            break;
        } else {
            lock_release(count_lock);
        }
        int rope_id = random() % NROPES;

        lock_acquire(rope_locks[rope_id]);
        if (rope_cut[rope_id]) {
            lock_release(rope_locks[rope_id]);
            continue;
        }

        rope_cut[rope_id] = true;
        lock_acquire(print_lock);
        kprintf("Dandelion severed rope %d\n", rope_id);
        lock_release(print_lock);

        lock_release(rope_locks[rope_id]);

        lock_acquire(count_lock);
        ropes_left--;
        lock_release(count_lock);

        thread_yield();
    }

    thread_exit_notify();
    lock_acquire(print_lock);
    kprintf("Dandelion thread done\n");
    lock_release(print_lock);

    thread_exit();
}

static void
marigold(void* p, unsigned long arg) {
    (void)p;
    (void)arg;

    lock_acquire(print_lock);
    kprintf("Marigold thread starting\n");
    lock_release(print_lock);

    while (true) {
        lock_acquire(count_lock);
        if (ropes_left == 0) {
            cv_broadcast(all_ropes_gone, count_lock);
            lock_release(count_lock);
            break;
        } else {
            lock_release(count_lock);
        }
        int stake_id = random() % NROPES;
        int rope_id = rope_to_stake[stake_id];

        lock_acquire(rope_locks[rope_id]);

        // recheck that the rope has not been swapped while we were reading it
        if (stake_to_rope[rope_id] != stake_id) {
            lock_release(rope_locks[rope_id]);
            continue;
        }
        if (rope_cut[rope_id]) {
            lock_release(rope_locks[rope_id]);
            continue;
        }

        rope_cut[rope_id] = true;

        lock_acquire(print_lock);
        kprintf("Marigold severed rope %d from stake %d\n", rope_id, stake_id);
        lock_release(print_lock);

        lock_release(rope_locks[rope_id]);

        lock_acquire(count_lock);
        ropes_left--;
        lock_release(count_lock);

        thread_yield();
    }

    thread_exit_notify();

    lock_acquire(print_lock);
    kprintf("Marigold thread done\n");
    lock_release(print_lock);

    thread_exit();
}

static void
flowerkiller(void* p, unsigned long arg) {
    (void)p;
    (void)arg;

    lock_acquire(print_lock);
    kprintf("Lord FlowerKiller thread starting\n");
    lock_release(print_lock);

    while (true) {
        lock_acquire(count_lock);
        if (ropes_left == 0) {
            lock_release(count_lock);
            break;
        }
        lock_release(count_lock);

        int first = random() % NROPES;
        int second = random() % NROPES;

        int fRope = rope_to_stake[first];
        int sRope = rope_to_stake[second];

		// there is a change that fRope and sRope are the same. Let's check if they are and if so, continue.
		if (fRope == sRope) {
            continue;
        }

        if (sRope < fRope) {
            int temp = fRope;
            fRope = sRope;
            sRope = temp;

            temp = first;
            first = second;
            second = temp;
        }

        lock_acquire(rope_locks[fRope]);
        lock_acquire(rope_locks[sRope]);

        if (rope_cut[fRope] || rope_cut[sRope]) {
            lock_release(rope_locks[fRope]);
            lock_release(rope_locks[sRope]);
            continue;
        }

        rope_to_stake[first] = sRope;
        rope_to_stake[second] = fRope;
        stake_to_rope[fRope] = second;
        stake_to_rope[sRope] = first;

        lock_acquire(print_lock);
        kprintf("Lord FlowerKiller switched rope %d from stake %d to stake %d\n", fRope, first, second);
        kprintf("Lord FlowerKiller switched rope %d from stake %d to stake %d\n", sRope, second, first);
        lock_release(print_lock);

        lock_release(rope_locks[fRope]);
        lock_release(rope_locks[sRope]);

        thread_yield();
    }

    thread_exit_notify();

    lock_acquire(print_lock);
    kprintf("Lord FlowerKiller thread done\n");
    lock_release(print_lock);

    thread_exit();
}

static
void
balloon(void *p, unsigned long arg)
{
	(void)p;
	(void)arg;

    lock_acquire(print_lock);
    kprintf("Balloon thread starting\n");
    lock_release(print_lock);

    lock_acquire(count_lock);

    while(ropes_left > 0) {
        cv_wait(all_ropes_gone, count_lock);
    }
    lock_release(count_lock);

    lock_acquire(print_lock);
    kprintf("Balloon freed and Prince Dandelion escapes!\n");
    lock_release(print_lock);

    // remove itself from threads_remaining
    thread_exit_notify();

    lock_acquire(print_lock);
    kprintf("Balloon thread done\n");
    lock_release(print_lock);

    thread_exit();
}

static void airballoon_init(void) {
    ropes_left = NROPES;

    int i;

    // Initialize rope mappings and locks
    for (i = 0; i < NROPES; i++) {
        rope_to_stake[i] = i;
        stake_to_rope[i] = i;
        rope_cut[i] = false;

        rope_locks[i] = lock_create("rope_lock");
        if (rope_locks[i] == NULL) {
            panic("airballoon_init: Failed to create rope lock %d", i);
        }
    }

    // Global locks and CVs
    count_lock = lock_create("count_lock");
    if (count_lock == NULL) {
        panic("airballoon_init: failed to create count_lock");
    }

    all_ropes_gone = cv_create("all_ropes_gone");
    if (all_ropes_gone == NULL) {
        panic("airballoon_init: failed to create all_ropes_gone CV");
    }

    threads_lock = lock_create("threads_lock");
    if (threads_lock == NULL) {
        panic("airballoon_init: failed to create threads_lock");
    }

    print_lock = lock_create("print_lock");
    if (print_lock == NULL) {
        panic("airballoon_init: failed to create print_lock");
    }

    threads_done_cv = cv_create("threads_done_cv");
    if (threads_done_cv == NULL) {
        panic("airballoon_init: failed to create threads_done_cv");
    }

    // Set the number of threads to spawn (main will wait for these)
    threads_remaining = 2 + N_LORD_FLOWERKILLER + 1;
    // 2 = Marigold + Dandelion, +N_LORD_FLOWERKILLER, +1 Balloon
}

static void airballoon_cleanup(void) {
    int i;

    // Destroy per-rope locks
    for (i = 0; i < NROPES; i++) {
        if (rope_locks[i] != NULL) {
            lock_destroy(rope_locks[i]);
            rope_locks[i] = NULL;
        }
    }

    // Destroy global count lock
    if (count_lock != NULL) {
        lock_destroy(count_lock);
        count_lock = NULL;
    }

    // Destroy all_ropes_gone CV
    if (all_ropes_gone != NULL) {
        cv_destroy(all_ropes_gone);
        all_ropes_gone = NULL;
    }

    if (print_lock != NULL) {
        lock_destroy(print_lock);
        print_lock = NULL;
    }

    // Destroy threads_lock and thread cv
    if (threads_lock != NULL) {
        lock_destroy(threads_lock);
        threads_lock = NULL;
    }

    if (threads_done_cv != NULL) {
        cv_destroy(threads_done_cv);
        threads_done_cv = NULL;
    }
}

// Change this function as necessary
int airballoon(int nargs, char** args) {
    int err = 0, i;

    // No idea what this is for so I removed it
    // char name[32];

    (void)nargs;
    (void)args;

    // Initialize data structures and stuff
    airballoon_init();

    // Spawn the threads
    err = thread_fork("Marigold", NULL, marigold, NULL, 0);
    if (err)
        goto panic;

    err = thread_fork("Dandelion", NULL, dandelion, NULL, 0);
    if (err)
        goto panic;

    for (i = 0; i < N_LORD_FLOWERKILLER; i++) {
        err = thread_fork("Lord FlowerKiller Thread", NULL, flowerkiller, NULL, 0);
        if (err)
            goto panic;
    }

    err = thread_fork("Balloon", NULL, balloon, NULL, 0);
    if (err)
        goto panic;

    // Wait for all threads to finish
    lock_acquire(threads_lock);
    while (threads_remaining > 0) {
        cv_wait(threads_done_cv, threads_lock);
    }
    lock_release(threads_lock);

    lock_acquire(print_lock);
    kprintf("Main thread done\n");
    lock_release(print_lock);

    // Clean up
    airballoon_cleanup();

    return 0;

panic:
    panic("airballoon: thread_fork failed: %s\n", strerror(err));
    return err;
}
