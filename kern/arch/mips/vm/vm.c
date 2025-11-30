#include <types.h>
#include <thread.h>
#include <kern/errno.h>
#include <vm.h>
#include <synch.h>

struct coremap *cm;
struct spinlock cm_spinlock = SPINLOCK_INITIALIZER;
volatile size_t cm_page_count;

pp_num_t first_page;
pp_num_t last_page;

static inline bool is_pp_used(pp_num_t pp_num) {
    KASSERT(pp_num < last_page);

    return cm->entries[pp_num].used;
}

/* Function to find n free pages from a start point */
static int find_free_pp(size_t npages, pp_num_t* start) {
    pp_num_t current = *start;

    if (npages > last_page || current > last_page - npages) {
        return ENOMEM;
    }

    while (current + npages <= last_page) {
        /* Iterate through the npages to determine if it is occupied */
        size_t pp_offset = 0;
        while (pp_offset < npages && !is_pp_used(current + pp_offset)) {
            pp_offset++;
        }
        if (pp_offset == npages) {
            *start = current;
            return 0;
        }

        /* Hop to check the next unused block */
        current = current + pp_offset + 1;
    }

    return ENOMEM;
}

static void kalloc_ppage(pp_num_t pp_num) {
    cm->entries[pp_num].used = true;
    cm->entries[pp_num].pp_num = pp_num;
    cm->entries[pp_num].kmalloc_end = false;
    cm_page_count++;
}

static inline void free_ppage(pp_num_t p) {
    KASSERT(first_page <= p && p < last_page);

    cm->entries[p].used = false;
    cm->entries[p].kmalloc_end = false;
    cm_page_count--;
}

void vm_bootstrap() {
    paddr_t paddr_start = 0;
    paddr_t paddr_end = ram_getsize();

    size_t total_pages = (paddr_end - paddr_start) / PAGE_SIZE;
    /* 
     * Calculate how many bytes the coremap will need.
     */
    size_t coremap_bytes = sizeof(struct coremap) + total_pages * sizeof(struct cm_entry);
    size_t coremap_pages = (coremap_bytes + PAGE_SIZE - 1) / PAGE_SIZE;

    paddr_t cm_paddr = ram_stealmem(coremap_pages);

    KASSERT(cm_paddr != 0);

    cm = (struct coremap*)PADDR_TO_KVADDR(cm_paddr);

    /* Set up entries array. We treat the struct and the array as they are one contiguous memory block */
    cm->entries = (struct cm_entry*)(PADDR_TO_KVADDR(cm_paddr) + sizeof(struct coremap));

    for (size_t i = 0; i < total_pages; i++) {
        cm->entries[i].used = false;
        cm->entries[i].kmalloc_end = false;
        cm->entries[i].dirty = false;
        cm->entries[i].pp_num = 0;
    }

    cm_page_count = 0;

    /* Recompute the total pages */
    KASSERT(ram_stealmem(0) % PAGE_SIZE == 0);
    first_page = PADDR_TO_PPAGE(ram_stealmem(0));
    last_page = PADDR_TO_PPAGE(ram_getsize());

    spinlock_acquire(&cm_spinlock);

    /* Mark memory as used for kernel pages */
    for (pp_num_t pp_num = 0; pp_num < first_page; pp_num++) {
        kalloc_ppage(pp_num);
    }

    spinlock_release(&cm_spinlock);
}

int vm_fault(int faulttype, vaddr_t faultaddress) {
    (void)faulttype;
    (void)faultaddress;
    return 0;
}

vaddr_t alloc_kpages(unsigned npages) {
    bool held_spinlock = spinlock_do_i_hold(&cm_spinlock);

    if (!held_spinlock) {
        spinlock_acquire(&cm_spinlock);
    }
    

    pp_num_t start = first_page;
    int result = find_free_pp(npages, &start);

    if (result) {
        if (!held_spinlock) {
            spinlock_release(&cm_spinlock);
        }
        return 0;
    }

    for (pp_num_t pp = start; pp < (pp_num_t)(start + npages); pp++) {
        kalloc_ppage(pp);
    }

    /* Mark end of block for kfree later */
    cm->entries[start + npages - 1].kmalloc_end = true;

    if (!held_spinlock) {
        spinlock_release(&cm_spinlock);
    }

    vaddr_t kvaddr = PADDR_TO_KVADDR(PPAGE_TO_PADDR(start));
    return kvaddr;
}

void free_kpages(vaddr_t addr) {
    bool held_spinlock = spinlock_do_i_hold(&cm_spinlock);

    if (!held_spinlock) {
        spinlock_acquire(&cm_spinlock);
    }

    pp_num_t curr = PADDR_TO_PPAGE(KVADDR_TO_PADDR(addr));

    /* 
     * Walk down, starting from the page we want to free,
     * until we find the page with the KMALLOC_END bit set.
     */
    bool end;
    while (is_pp_used(curr)) {
        end = cm->entries[curr].kmalloc_end;
        free_ppage(curr);
        if (end) {
            if (!held_spinlock) {
                spinlock_release(&cm_spinlock);
            }
            return;
        }
        curr++;
    }

    if (!held_spinlock) {
        spinlock_release(&cm_spinlock);
    }
}

void vm_tlbshootdown_all(void) {
}

void vm_tlbshootdown(const struct tlbshootdown *tlb) {
    (void)tlb;
}