#include <types.h>
#include <addrspace.h>
#include <kern/errno.h>
#include <kern/unistd.h>
#include <lib.h>
#include <pagetable.h>
#include <vm.h>

static int l2_ptable_copy(struct l2_ptable *src, struct l2_ptable **ret) {
    struct l2_ptable *newtable = kmalloc(sizeof(struct l2_ptable));

    if (!newtable) {
        return ENOMEM;
    }

    for (int i = 0; i < L2_SIZE; i++) {
        newtable->entries[i] = src->entries[i];
    }

    *ret = newtable;
    return 0;
}

static void l2_ptable_destroy(struct l2_ptable *l2) {
    if (!l2) {
        return;
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

    /* Extract VPN from vaddr (remove page offset) */
    uint32_t vpn = vaddr >> 12;  /* 4 KB pages → 12-bit offset */

    /* Split VPN into L1 and L2 indices */
    uint32_t l1_index = (vpn >> 10) & 0x3FF;  /* top 10 bits of VPN */
    uint32_t l2_index = vpn & 0x3FF;          /* bottom 10 bits of VPN */

    struct l2_ptable* l2 = pt->l2_entries[l1_index];
    if (l2 == NULL) {
        return NULL;  /* L2 table does not exist */
    }

    return &l2->entries[l2_index];
}
