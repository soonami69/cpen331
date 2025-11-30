#ifndef _PAGETABLE_H_
#define _PAGETABLE_H_

#include <types.h>
#include <kern/limits.h>

#define L2_SIZE 1024 /* We use 10 bits because vaddr_t is 32 bits so we can utilise all of it */
#define L1_SIZE 1024 /* The same as L2 size cause why not */

struct pte {
    bool valid; /* Is the page in physical memory? */
    bool readonly; /* Is the page read-only? */
    bool dirty;    /* Has the page been written to? */
    paddr_t ppn; /* Physical page number */
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

#endif