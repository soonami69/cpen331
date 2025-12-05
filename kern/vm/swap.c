#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <lib.h>
#include <uio.h>
#include <vfs.h>
#include <vnode.h>
#include <synch.h>
#include <vm.h>
#include <swap.h>

static struct vnode *swap_vnode;
static struct lock *swap_lock;

int swap_bootstrap(void) {
    KASSERT(swap_vnode == NULL);
    KASSERT(swap_lock == NULL);

    swap_lock = lock_create("swaplock");
    if (swap_lock == NULL) {
        return ENOMEM;
    }

    int result = vfs_open("lhd0raw:", O_RDWR, 0, &swap_vnode);
    if (result) {
        lock_destroy(swap_lock);
        swap_lock = NULL;
        return result;
    }

    return 0;
}

int swap_write_page(paddr_t paddr, off_t offset) {
    KASSERT(swap_vnode != NULL);
    KASSERT(swap_lock != NULL);
    KASSERT((offset % PAGE_SIZE) == 0);

    struct iovec iov;
    struct uio ku;

    void *kaddr = (void *)PADDR_TO_KVADDR(paddr);
    uio_kinit(&iov, &ku, kaddr, PAGE_SIZE, offset, UIO_WRITE);

    lock_acquire(swap_lock);
    int result = VOP_WRITE(swap_vnode, &ku);
    lock_release(swap_lock);

    if (result) {
        return result;
    }

    /* We expect to write the full page. */
    if (ku.uio_resid != 0) {
        return EIO;
    }

    return 0;
}
