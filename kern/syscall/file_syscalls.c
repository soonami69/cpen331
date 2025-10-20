#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <kern/stat.h>
#include <kern/seek.h>
#include <lib.h>
#include <uio.h>
#include <thread.h>
#include <current.h>
#include <proc.h>
#include <vnode.h>
#include <syscall.h>
#include <limits.h>
#include <copyinout.h>
#include <fdtable.h>
#include <file.h>
#include <vfs.h>

int
sys_open(const_userptr_t user_filename, int flags, mode_t mode, int *retval)
{
    char *filename;
    struct file *f;
    int result;

    filename = kmalloc(PATH_MAX);
    if (filename == NULL) {
        return ENOMEM;
    }

    result = copyinstr(user_filename, filename, PATH_MAX, NULL);
    if (result) {
        kfree(filename);
        return result;
    }

    result = file_open(filename, flags, mode, &f);
    kfree(filename);
    if (result) {
        return result;
    }

    result = fdtable_add(curproc->p_fdtable, f, retval);
    if (result) {
        file_close(f);
        return result;
    }

    return 0;
}

int
sys_read(int fd, userptr_t buf, size_t buflen, int *retval)
{
    struct iovec iov;
    struct uio ku;
    struct file *f;
    int result;

    result = fdtable_get(curproc->p_fdtable, fd, &f);
    if (result) {
        return result;
    }

    if ((f->openflags & O_ACCMODE) == O_WRONLY) {
        return EBADF;
    }

    lock_acquire(f->f_offsetlock);
    uio_kinit(&iov, &ku, buf, buflen, f->f_offset, UIO_READ);

    result = VOP_READ(f->f_vnode, &ku);
    if (result) {
        lock_release(f->f_offsetlock);
        return result;
    }

    f->f_offset = ku.uio_offset;
    lock_release(f->f_offsetlock);

    *retval = buflen - ku.uio_resid;
    return 0;
}

int
sys_write(int fd, userptr_t buf, size_t buflen, int *retval)
{
    struct iovec iov;
    struct uio ku;
    struct file *f;
    int result;

    result = fdtable_get(curproc->p_fdtable, fd, &f);
    if (result) {
        return result;
    }

    if ((f->openflags & O_ACCMODE) == O_RDONLY) {
        return EBADF;
    }

    lock_acquire(f->f_offsetlock);
    uio_kinit(&iov, &ku, buf, buflen, f->f_offset, UIO_WRITE);

    result = VOP_WRITE(f->f_vnode, &ku);
    if (result) {
        lock_release(f->f_offsetlock);
        return result;
    }

    f->f_offset = ku.uio_offset;
    lock_release(f->f_offsetlock);

    *retval = buflen - ku.uio_resid;
    return 0;
}

int
sys_lseek(int fd, off_t offset, int whence, off_t *retval)
{
    struct file *f;
    struct stat statbuf;
    off_t newpos;
    int result;

    result = fdtable_get(curproc->p_fdtable, fd, &f);
    if (result) {
        return result;
    }

    if (!VOP_ISSEEKABLE(f->f_vnode)) {
        return ESPIPE;
    }

    lock_acquire(f->f_offsetlock);

    switch (whence) {
        case SEEK_SET:
            newpos = offset;
            break;
        case SEEK_CUR:
            newpos = f->f_offset + offset;
            break;
        case SEEK_END:
            result = VOP_STAT(f->f_vnode, &statbuf);
            if (result) {
                return result;
            }
            newpos = statbuf.st_size + offset;
            break;
        default:
            lock_release(f->f_offsetlock);
            return EINVAL;
    }

    if (newpos < 0) {
        lock_release(f->f_offsetlock);
        return EINVAL;
    }

    f->f_offset = newpos;
    lock_release(f->f_offsetlock);

    *retval = newpos;
    return 0;
}

int
sys_close(int fd)
{
    struct file *f;
    int result;

    result = fdtable_remove(curproc->p_fdtable, fd, &f);
    if (result) {
        return result;
    }

    file_close(f);
    return 0;
}

int
sys_dup2(int oldfd, int newfd, int *retval)
{
    struct file *f, *oldf = NULL;
    int result;

    if (oldfd == newfd) {
        *retval = newfd;
        return 0;
    }

    result = fdtable_get(curproc->p_fdtable, oldfd, &f);
    if (result) {
        return result;
    }

    result = fdtable_remove(curproc->p_fdtable, newfd, &oldf);
    if (result && result != EBADF) {
        return result;
    }

    if (oldf != NULL) {
        file_close(oldf);
    }

    file_incref(f);

    result = fdtable_set(curproc->p_fdtable, newfd, f);
    if (result) {
        return result;
    }

    *retval = newfd;
    return 0;
}

int
sys_chdir(const_userptr_t user_path)
{
    char *path;
    int result;

    path = kmalloc(PATH_MAX);
    if (path == NULL) {
        return ENOMEM;
    }

    result = copyinstr(user_path, path, PATH_MAX, NULL);
    if (result) {
        kfree(path);
        return result;
    }

    result = vfs_chdir(path);
    kfree(path);

    return result;
}

int
sys___getcwd(userptr_t user_buf, size_t buflen, int *retval)
{
    char *buf;
    struct iovec iov;
    struct uio ku;
    int result;

    buf = kmalloc(buflen);
    if (buf == NULL) {
        return ENOMEM;
    }

    uio_kinit(&iov, &ku, buf, buflen, 0, UIO_READ);
    result = vfs_getcwd(&ku);
    if (result) {
        kfree(buf);
        return result;
    }

    result = copyout(buf, user_buf, buflen - ku.uio_resid);
    kfree(buf);
    if (result) {
        return result;
    }

    *retval = buflen - ku.uio_resid;
    return 0;
}
