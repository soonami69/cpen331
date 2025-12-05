/*
 * Swap device helpers for paging pages out to disk.
 */

#ifndef _SWAP_H_
#define _SWAP_H_

#include <types.h>

/* Set up access to the raw swap device. */
int swap_bootstrap(void);

/* Write a physical page to the swap device at the given byte offset. */
int swap_write_page(paddr_t paddr, off_t offset);

#endif /* _SWAP_H_ */
