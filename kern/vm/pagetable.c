#include <types.h>
#include <addrspace.h>
#include <kern/errno.h>
#include <kern/unistd.h>
#include <lib.h>
#include <pagetable.h>
#include <vm.h>
#include <swap.h>

static int copy_entry(struct pte *src, struct pte *ret) {
    if (!src->valid) {
        /* No page, copy the metadata */
        *ret = *src;
        return 0;
    }

    vaddr_t kvaddr = alloc_kpages(1);
    if (kvaddr == 0) {
        return ENOMEM;
    }

    vaddr_t parent_kv = PADDR_TO_KVADDR(PPAGE_TO_PADDR(src->ppn));
    memcpy((void*)kvaddr, (void*)parent_kv, PAGE_SIZE);

    /* Fill in metadata */
    ret->valid = true;
    ret->in_mem = true;
    ret->readonly = src->readonly;
    ret->dirty = false;
    ret->swap_offset = src->swap_offset;
    ret->ppn = PADDR_TO_PPAGE(KVADDR_TO_PADDR(kvaddr));

    return 0;
}

static int l2_ptable_copy(struct l2_ptable *src, struct l2_ptable **ret) {
    struct l2_ptable *newtable = kmalloc(sizeof(struct l2_ptable));

    if (!newtable) {
        return ENOMEM;
    }

    for (int i = 0; i < L2_SIZE; i++) {
        int err = copy_entry(&src->entries[i], &newtable->entries[i]);

        if (err) {
            /* Cleanup previously allocated pages */
            for (int j = 0; j < i; j++) {
                if (newtable->entries[j].valid && newtable->entries[j].in_mem) {
                    free_kpages(PPAGE_TO_PADDR(PADDR_TO_KVADDR(newtable->entries[j].ppn)));
                }
            }
            kfree(newtable);
            return ENOMEM;
        }
    }

    *ret = newtable;
    return 0;
}

static void l2_ptable_destroy(struct l2_ptable *l2) {
    if (!l2) {
        return;
    }

    for (int i = 0; i < L2_SIZE; i++) {
        if (l2->entries[i].valid && !l2->entries[i].in_mem && l2->entries[i].swap_offset != SWAP_OFFSET_NONE) {
            swap_free_slot(l2->entries[i].swap_offset);
        }
    }

    kfree(l2);
}

struct pagetable* pagetable_create(void) {
    struct pagetable* pt = kmalloc(sizeof(struct pagetable));
    if (pt == NULL) {
        return NULL;
    }

    for (int i = 0; i < L1_SIZE; i++) {
        pt->l2_entries[i] = NULL;
    }

    return pt;
}

void pagetable_destroy(struct pagetable* pt) {
    KASSERT(pt != NULL);

    for (int i = 0; i < L1_SIZE; i++) {
        if (pt->l2_entries[i] != NULL) {
            l2_ptable_destroy(pt->l2_entries[i]);
            pt->l2_entries[i] = NULL;
        }
    }
    kfree(pt);
}

int pagetable_copy(struct pagetable *src, struct pagetable **ret) {
    struct pagetable* newpt = kmalloc(sizeof(struct pagetable));

    if (!newpt) {
        return ENOMEM;
    }

    int err;
    for (int i = 0; i < L1_SIZE; i++) {
        if (src->l2_entries[i]) {
            err = l2_ptable_copy(src->l2_entries[i], &newpt->l2_entries[i]);

            if (err) {
                for (int j = 0; j < i; j++) {
                    if (newpt->l2_entries[j]) {
                        l2_ptable_destroy(newpt->l2_entries[j]);
                    }
                }
                kfree(newpt);
                return err;
            }
        } else {
            newpt->l2_entries[i] = NULL;
        }
    }

    *ret = newpt;
    return 0;
}

/*
 * Lookup a page table entry given a virtual page number.
 */
struct pte* pagetable_lookup(struct pagetable* pt, vaddr_t vaddr) {
    KASSERT(pt != NULL);

    /* Split VPN into L1 and L2 indices */
    pt_idx_t l1_index = GET_L1_INDEX(vaddr);  /* top 10 bits of VPN */
    pt_idx_t l2_index = GET_L2_INDEX(vaddr); /* bottom 10 bits of VPN */

    struct l2_ptable* l2 = pt->l2_entries[l1_index];
    if (l2 == NULL) {
        return NULL;  /* L2 table does not exist */
    }

    return &l2->entries[l2_index];
}

int pagetable_insert(struct pagetable *pt, vaddr_t vaddr, paddr_t paddr, bool readonly) {
    KASSERT(pt != NULL);
    KASSERT(paddr != 0);

    pt_idx_t l1_index = GET_L1_INDEX(vaddr);
    pt_idx_t l2_index = GET_L2_INDEX(vaddr);

    /* Create new l2 table if it doesn't exist */
    if (pt->l2_entries[l1_index] == NULL) {
        /* 
         *A call to alloc_kpages might be faster since the l2_table
         * fits into one page, but this seems safer
         */
        struct l2_ptable *new_l2 = kmalloc(sizeof(struct l2_ptable));

        if (new_l2 == NULL) {
            return ENOMEM;
        }

        bzero((void*)new_l2, sizeof(struct l2_ptable));

        pt->l2_entries[l1_index] = new_l2;
    }

    struct l2_ptable *l2 = pt->l2_entries[l1_index];
    struct pte *entry = &l2->entries[l2_index];

    entry->ppn = PADDR_TO_PPAGE(paddr);
    entry->in_mem = true;
    entry->valid = true;
    entry->readonly = readonly;
    entry->dirty = false;
    entry->swap_offset = SWAP_OFFSET_NONE;

    return 0;
}
