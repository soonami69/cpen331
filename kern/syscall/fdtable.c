#include <types.h>
#include <kern/errno.h>
#include <limits.h>
#include <lib.h>
#include <file.h>
#include <fdtable.h>

struct fdtable*
fdtable_init(void)
{
    struct fdtable *fdt = kmalloc(sizeof(struct fdtable));
    if (fdt == NULL) {
        return NULL;
    }

    fdt->fd_files = kmalloc(sizeof(struct file*) * OPEN_MAX);
    if (fdt->fd_files == NULL) {
        kfree(fdt);
        return NULL;
    }

    for (int i = 0; i < OPEN_MAX; i++) {
        fdt->fd_files[i] = NULL;
    }

    return fdt;
}

void
fdtable_destroy(struct fdtable *fdt)
{
    if (fdt == NULL) {
        return;
    }

    for (int i = 0; i < OPEN_MAX; i++) {
        if (fdt->fd_files[i] != NULL) {
            file_close(fdt->fd_files[i]);
        }
    }

    kfree(fdt->fd_files);
    kfree(fdt);
}

int
fdtable_add(struct fdtable *fdt, struct file* vn, int *retval)
{
    for (int i = 0; i < OPEN_MAX; i++) {
        if (fdt->fd_files[i] == NULL) {
            fdt->fd_files[i] = vn;
            if (retval != NULL) {
                *retval = i;
            }
            return 0;
        }
    }
    return EMFILE;
}

int
fdtable_get(struct fdtable *fdt, int fd, struct file** retval)
{
    if (fd < 0 || fd >= OPEN_MAX) {
        return EBADF;
    }
    if (fdt->fd_files[fd] == NULL) {
        return EBADF;
    }
    *retval = fdt->fd_files[fd];
    return 0;
}

int
fdtable_remove(struct fdtable *fdt, int fd, struct file** retval)
{
    if (fd < 0 || fd >= OPEN_MAX) {
        return EBADF;
    }
    if (fdt->fd_files[fd] == NULL) {
        return EBADF;
    }
    if (retval != NULL) {
        *retval = fdt->fd_files[fd];
    }
    fdt->fd_files[fd] = NULL;
    return 0;
}

int
fdtable_set(struct fdtable *fdt, int fd, struct file* vn)
{
    if (fd < 0 || fd >= OPEN_MAX) {
        return EBADF;
    }
    fdt->fd_files[fd] = vn;
    return 0;
}

/* Helper to clone a fdtable */
struct fdtable* fdtable_clone(struct fdtable* parent_fdt) {
    if (parent_fdt == NULL) {
        return NULL;
    }

    struct fdtable* child_fdt = fdtable_init();
    if (child_fdt == NULL) {
        return NULL;
    }

    for (int fd = 0; fd < OPEN_MAX; fd++) {
        struct file* f = NULL;
        if (fdtable_get(parent_fdt, fd, &f) == 0 && f != NULL) {
            // Increment refcount on the file object
            file_incref(f);

            // Set the file in the child FD table
            if (fdtable_set(child_fdt, fd, f) != 0) {
                // On failure, clean up
                fdtable_destroy(child_fdt);
                return NULL;
            }
        }
    }

    return child_fdt;
}
