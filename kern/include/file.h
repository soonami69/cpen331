#ifndef FILE_H
#define FILE_H

#include <vnode.h>
#include <synch.h>

struct file {
    struct vnode *f_vnode;
    int openflags;
    off_t f_offset;
    int f_refcount;
    struct lock *f_offsetlock;
    struct lock *f_refcountlock;
};

struct file *file_create(struct vnode *vn, int openflags);
int file_open(const char *pathname, int openflags, mode_t mode, struct file **ret);
void file_destroy(struct file *f);
void file_close(struct file *f);
void file_incref(struct file *f);

#endif /* FILE_H */
