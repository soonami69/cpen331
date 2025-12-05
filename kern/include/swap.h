/*
 * Swap device helpers for paging pages out to disk.
 */

#ifndef _SWAP_H_
#define _SWAP_H_

#include <types.h>

#define SWAP_OFFSET_NONE ((off_t)-1)
#define SWAP_SLOT_NONE   (-1)

/* Set up access to the raw swap device. */
int swap_bootstrap(void);

/* Allocate a swap slot. */
int swap_alloc_slot(off_t *offset);

/* Free a previously allocated swap slot. */
void swap_free_slot(off_t offset);

/* Write a physical page to a swap slot. */
int swap_write_page(paddr_t paddr, off_t offset);

/* Read a physical page from the swap device at the given byte offset. */
int swap_read_page(paddr_t paddr, off_t offset);

#endif /* _SWAP_H_ */
