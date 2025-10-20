#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <vfs.h>
#include <vnode.h>
#include <file.h>

struct file *
file_create(struct vnode *vn, int openflags)
{
    struct file *f = kmalloc(sizeof(struct file));
    if (f == NULL) {
        return NULL;
    }

    f->f_vnode = vn;
    f->openflags = openflags;
    f->f_offset = 0;
    f->f_refcount = 1;
    f->f_offsetlock = lock_create("file_offset_lock");
    if (f->f_offsetlock == NULL) {
        kfree(f);
        return NULL;
    }
    f->f_refcountlock = lock_create("file_refcount_lock");
    if (f->f_refcountlock == NULL) {
        lock_destroy(f->f_offsetlock);
        kfree(f);
        return NULL;
    }

    return f;
}

int
file_open(const char *pathname, int openflags, mode_t mode, struct file **ret)
{
    struct vnode *vn;
    struct file *f;
    int result;

    result = vfs_open(kstrdup(pathname), openflags, mode, &vn);
    if (result) {
        return result;
    }

    f = file_create(vn, openflags);
    if (f == NULL) {
        vfs_close(vn);
        return ENOMEM;
    }

    *ret = f;
    return 0;
}

void
file_destroy(struct file *f)
{
    vfs_close(f->f_vnode);
    lock_destroy(f->f_offsetlock);
    lock_destroy(f->f_refcountlock);
    kfree(f);
}

void
file_close(struct file *f)
{
    lock_acquire(f->f_refcountlock);
    f->f_refcount--;
    if (f->f_refcount == 0) {
        lock_release(f->f_refcountlock);
        file_destroy(f);
    } else {
        lock_release(f->f_refcountlock);
    }
}

void
file_incref(struct file *f)
{
    lock_acquire(f->f_refcountlock);
    f->f_refcount++;
    lock_release(f->f_refcountlock);
}
