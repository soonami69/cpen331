#ifndef _PAGETABLE_H_
#define _PAGETABLE_H_

#include <types.h>
#include <kern/limits.h>

#define L2_SIZE 1024 /* We use 10 bits because vaddr_t is 32 bits so we can utilise all of it */
#define L1_SIZE 1024 /* The same as L2 size cause why not */
#define L1_PAGE_MASK 0x003ff000 /* Mask to get L1 page index */

typedef __u32 pt_idx_t;

#define GET_L2_INDEX(vaddr) (vaddr >> 22)
#define GET_L1_INDEX(vaddr) ((vaddr & L1_PAGE_MASK) >> 12)

/* 
 * This struct is an entry in our pagetable. It could possibly
 * be packed into a 32-bit value as ppn is max 20 bits but
 * this is much more readable.
 */
struct pte {
    bool valid; /* Is the page supposed to exist? */
    bool in_mem; /* Is the page in phyiscal memory? */
    bool readonly; /* Is the page read-only? */
    bool dirty;    /* Has the page been written to? */
    pp_num_t ppn; /* Physical page number */
};

/* A two-level page table structure */
struct l2_ptable {
    struct pte entries[L2_SIZE];
};

struct pagetable {
    struct l2_ptable* l2_entries[L1_SIZE];
};

struct pagetable *pagetable_create(void);

void pagetable_destroy(struct pagetable *pt);

int pagetable_copy(struct pagetable *src, struct pagetable **ret);

/* Get an entry given a virtual page number */
struct pte* pagetable_lookup(struct pagetable* pt, vaddr_t vpn);

int pagetable_insert(struct pagetable *pt, vaddr_t vaddr, paddr_t paddr, bool readonly);

#endif