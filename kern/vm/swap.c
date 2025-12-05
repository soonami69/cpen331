#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <kern/stat.h>
#include <lib.h>
#include <uio.h>
#include <vfs.h>
#include <vnode.h>
#include <synch.h>
#include <vm.h>
#include <swap.h>
#include <bitmap.h>

static struct vnode *swap_vnode;
static struct lock *swap_lock;
static struct bitmap *swap_bitmap;
static unsigned swap_slots;

int swap_bootstrap(void) {
    KASSERT(swap_vnode == NULL);
    KASSERT(swap_lock == NULL);
    KASSERT(swap_bitmap == NULL);

    swap_lock = lock_create("swaplock");
    if (swap_lock == NULL) {
        return ENOMEM;
    }

    int result = vfs_open(kstrdup("lhd0raw:"), O_RDWR, 0, &swap_vnode);
    if (result) {
        lock_destroy(swap_lock);
        swap_lock = NULL;
        return result;
    }

    struct stat st;
    result = VOP_STAT(swap_vnode, &st);
    if (result) {
        vfs_close(swap_vnode);
        swap_vnode = NULL;
        lock_destroy(swap_lock);
        swap_lock = NULL;
        return result;
    }

    swap_slots = st.st_size / PAGE_SIZE;
    if (swap_slots == 0) {
        vfs_close(swap_vnode);
        swap_vnode = NULL;
        lock_destroy(swap_lock);
        swap_lock = NULL;
        return ENOSPC;
    }

    swap_bitmap = bitmap_create(swap_slots);
    if (swap_bitmap == NULL) {
        vfs_close(swap_vnode);
        swap_vnode = NULL;
        lock_destroy(swap_lock);
        swap_lock = NULL;
        return ENOMEM;
    }

    return 0;
}

int swap_alloc_slot(off_t *offset) {
    KASSERT(offset != NULL);
    KASSERT(swap_bitmap != NULL);

    lock_acquire(swap_lock);

    unsigned idx;
    int result = bitmap_alloc(swap_bitmap, &idx);
    if (result) {
        lock_release(swap_lock);
        return ENOSPC;
    }

    *offset = (off_t)idx * PAGE_SIZE;

    lock_release(swap_lock);
    return 0;
}

void swap_free_slot(off_t offset) {
    if (offset == SWAP_OFFSET_NONE) {
        return;
    }
    KASSERT(swap_bitmap != NULL);
    KASSERT((offset % PAGE_SIZE) == 0);

    unsigned idx = offset / PAGE_SIZE;
    KASSERT(idx < swap_slots);

    lock_acquire(swap_lock);
    bitmap_unmark(swap_bitmap, idx);
    lock_release(swap_lock);
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

int swap_read_page(paddr_t paddr, off_t offset) {
    KASSERT(swap_vnode != NULL);
    KASSERT(swap_lock != NULL);
    KASSERT((offset % PAGE_SIZE) == 0);

    struct iovec iov;
    struct uio ku;

    void *kaddr = (void *)PADDR_TO_KVADDR(paddr);
    uio_kinit(&iov, &ku, kaddr, PAGE_SIZE, offset, UIO_READ);

    lock_acquire(swap_lock);
    int result = VOP_READ(swap_vnode, &ku);
    lock_release(swap_lock);

    if (result) {
        return result;
    }

    if (ku.uio_resid != 0) {
        return EIO;
    }

    return 0;
}
