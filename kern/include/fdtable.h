#ifndef _FDTABLE_H_
#define _FDTABLE_H_

#include <file.h>

struct fdtable {
    struct file **fd_files;
};

struct fdtable *fdtable_init(void);
void fdtable_destroy(struct fdtable *fdt);
int fdtable_add(struct fdtable *fdt, struct file* f, int *retval);
int fdtable_get(struct fdtable *fdt, int fd, struct file** retval);
int fdtable_remove(struct fdtable *fdt, int fd, struct file** retval);
int fdtable_set(struct fdtable *fdt, int fd, struct file* f);
struct fdtable* fdtable_clone(struct fdtable* parent_fdt);

#endif /* _FDTABLE_H_ */
