#include <types.h>
#include <thread.h>
#include <kern/errno.h>
#include <addrspace.h>
#include <vm.h>
#include <synch.h>
#include <swap.h>
#include <proc.h>
#include <spl.h>
#include <mips/tlb.h>
#include <lib.h>

struct coremap *cm;
struct spinlock cm_spinlock = SPINLOCK_INITIALIZER;
struct spinlock tlb_spinlock = SPINLOCK_INITIALIZER;
volatile size_t cm_page_count;
static pp_num_t cm_evict_index = 0;

pp_num_t first_page;
pp_num_t last_page;

static inline bool is_pp_used(pp_num_t pp_num) {
    KASSERT(pp_num < last_page);

    return cm->entries[pp_num].used;
}

static inline void free_ppage(pp_num_t p) {
    KASSERT(first_page <= p && p < last_page);

    cm->entries[p].used = false;
    cm->entries[p].kmalloc_end = false;
    cm->entries[p].kernel_page = false;
    cm->entries[p].busy = false;
    cm->entries[p].owner = NULL;
    cm->entries[p].vaddr = 0;
    cm_page_count--;
}

static bool is_valid_address(struct addrspace* as, vaddr_t vaddr) {
    /* check regions */
    struct region* r = as->region_list;
    while (r != NULL) {
        if (vaddr >= r->as_vbase && vaddr < r->as_vbase + r->as_npages * PAGE_SIZE) {
            return true;
        }
        r = r->next;
    }

    /* Check heap */
    if (vaddr >= as->heap_start && vaddr < as->heap_end) {
        return true;
    }

    if (vaddr < USERSTACK && vaddr >= as->stack_base) {
        return true;
    }

    return false;
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
    cm->entries[pp_num].kernel_page = true;
    cm->entries[pp_num].busy = false;
    cm->entries[pp_num].owner = NULL;
    cm->entries[pp_num].vaddr = 0;
    cm_page_count++;
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
        cm->entries[i].kernel_page = false;
        cm->entries[i].busy = false;
        cm->entries[i].owner = NULL;
        cm->entries[i].vaddr = 0;
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

static int get_region_permissions(struct addrspace *as, vaddr_t vaddr, 
                                    bool *readable, bool *writeable, bool *executable) {
    KASSERT(as != NULL);

    struct region* r = as->region_list;
    while (r != NULL) {
        if (vaddr >= r->as_vbase && vaddr < r->as_vbase + r->as_npages * PAGE_SIZE) {
            *readable = r->read;
            *writeable = r->write;
            *executable = r->exec;
            return 0;
        }
        r = r->next;
    }

    /* Check heap, always read-write & not executable */
    if (vaddr >= as->heap_start && vaddr < as->heap_end) {
        *readable = true;
        *writeable = true;
        *executable = false;
        return 0;
    }

    /* Same with stack */
    if (vaddr < USERSTACK && vaddr >= as->stack_base) {
        *readable = true;
        *writeable = true;
        *executable = false;
        return 0;
    }

    return EFAULT;
}

static unsigned int tlb_next_victim = 0;

static void cm_set_user_page(pp_num_t ppn, struct addrspace *as, vaddr_t vaddr) {
    spinlock_acquire(&cm_spinlock);
    cm->entries[ppn].kernel_page = false;
    cm->entries[ppn].owner = as;
    cm->entries[ppn].vaddr = vaddr & PAGE_FRAME;
    spinlock_release(&cm_spinlock);
}

static int evict_one(pp_num_t *freed_ppn) {
    spinlock_acquire(&cm_spinlock);

    size_t total = last_page - first_page;
    for (size_t i = 0; i < total; i++) {
        pp_num_t candidate = first_page + ((cm_evict_index + i) % total);
        struct cm_entry *cme = &cm->entries[candidate];

        if (!cme->used || cme->kernel_page || cme->busy || cme->owner == NULL) {
            continue;
        }

        cme->busy = true;
        cm_evict_index = ((candidate - first_page) + 1) % total;
        spinlock_release(&cm_spinlock);

        struct addrspace *as = cme->owner;
        vaddr_t vaddr = cme->vaddr;

        KASSERT(as != NULL);
        lock_acquire(as->as_lock);
        struct pte *pte = pagetable_lookup(as->pt, vaddr);
        if (pte == NULL || !pte->valid || !pte->in_mem || pte->ppn != cme->pp_num) {
            lock_release(as->as_lock);
            spinlock_acquire(&cm_spinlock);
            cme->busy = false;
            spinlock_release(&cm_spinlock);
            continue;
        }

        off_t swap_offset;
        int result = swap_alloc_slot(&swap_offset);
        if (result) {
            lock_release(as->as_lock);
            spinlock_acquire(&cm_spinlock);
            cme->busy = false;
            spinlock_release(&cm_spinlock);
            return result;
        }

        result = swap_write_page(PPAGE_TO_PADDR(cme->pp_num), swap_offset);
        if (result) {
            lock_release(as->as_lock);
            spinlock_acquire(&cm_spinlock);
            cme->busy = false;
            spinlock_release(&cm_spinlock);
            swap_free_slot(swap_offset);
            return result;
        }

        pte->swap_offset = swap_offset;
        pte->in_mem = false;
        pte->dirty = false;
        pte->ppn = 0;

        struct tlbshootdown tlb;
        tlb.vaddr = vaddr;
        vm_tlbshootdown(&tlb);

        lock_release(as->as_lock);

        spinlock_acquire(&cm_spinlock);
        free_ppage(cme->pp_num);
        *freed_ppn = candidate;
        spinlock_release(&cm_spinlock);
        return 0;
    }

    spinlock_release(&cm_spinlock);
    return ENOMEM;
}

vaddr_t alloc_user_page(void) {
    vaddr_t kvaddr = alloc_kpages(1);
    if (kvaddr != 0) {
        return kvaddr;
    }

    pp_num_t freed = 0;
    int err = evict_one(&freed);
    if (err) {
        return 0;
    }

    return alloc_kpages(1);
}

void tlb_insert_entry(uint32_t entryhi, uint32_t entrylo) {
    /* We use a simple round robin strategy */
    int i;

    for (i = 0; i < NUM_TLB; i++) {
        uint32_t hi, lo;
        tlb_read(&hi, &lo, i);

        if (!(lo & TLBLO_VALID)) {
            tlb_write(entryhi, entrylo, i);
            return;
        }
    }

    tlb_write(entryhi, entrylo, tlb_next_victim);
    tlb_next_victim = (tlb_next_victim + 1) % NUM_TLB;
}

int vm_fault(int faulttype, vaddr_t faultaddress) {
    struct addrspace *as;
    int result;

    as = proc_getas();
    if (as == NULL) {
        return EFAULT;
    }

    /* Acquire lock for addresspace */
    lock_acquire(as->as_lock);

    /* Check if the fault address is within range */
    if (!is_valid_address(as, faultaddress)) {
        lock_release(as->as_lock);
        return EFAULT;
    }

    vaddr_t page_vaddr = faultaddress & PAGE_FRAME;
    struct pte* entry = pagetable_lookup(as->pt, page_vaddr);

    /* If no mapping exists, create a new page */
    if (entry == NULL || !entry->valid) {
        vaddr_t vaddr = alloc_user_page();

        if (vaddr == 0) {
            lock_release(as->as_lock);
            return ENOMEM;
        }

        /* Zero the page */
        bzero((void*)vaddr, PAGE_SIZE);

        /* Check region permissions */
        bool readable, writeable, executable;

        result = get_region_permissions(as, page_vaddr, &readable, &writeable, &executable);

        if (result) {
            free_kpages(vaddr);
            lock_release(as->as_lock);
            return EFAULT;
        }

        bool readonly = !writeable;

        paddr_t paddr = KVADDR_TO_PADDR(vaddr);

        result = pagetable_insert(as->pt, page_vaddr, paddr, readonly);
        
        if (result) {
            lock_release(as->as_lock);
            return result;
        }

        entry = pagetable_lookup(as->pt, page_vaddr);
        KASSERT(entry != NULL && entry->valid && entry->in_mem);
        cm_set_user_page(PADDR_TO_PPAGE(paddr), as, page_vaddr);
    } else if (!entry->in_mem) {
        vaddr_t vaddr = alloc_user_page();
        if (vaddr == 0) {
            lock_release(as->as_lock);
            return ENOMEM;
        }

        if (entry->swap_offset == SWAP_OFFSET_NONE) {
            free_kpages(vaddr);
            lock_release(as->as_lock);
            return EFAULT;
        }

        paddr_t paddr = KVADDR_TO_PADDR(vaddr);

        result = swap_read_page(paddr, entry->swap_offset);
        if (result) {
            free_kpages(vaddr);
            lock_release(as->as_lock);
            return result;
        }

        entry->ppn = PADDR_TO_PPAGE(paddr);
        entry->in_mem = true;
        entry->dirty = false;
        swap_free_slot(entry->swap_offset);
        entry->swap_offset = SWAP_OFFSET_NONE;

        cm_set_user_page(entry->ppn, as, page_vaddr);
    }

    /* Check permissions based on fault type */
    if (faulttype == VM_FAULT_READONLY && entry->readonly) {
        lock_release(as->as_lock);
        return EFAULT;
    }

    if (faulttype == VM_FAULT_WRITE) {
        entry->dirty = true;
    }

    /*
     * TLB SHENANIGANS HERE
     */
    uint32_t entryhi = faultaddress & TLBHI_VPAGE;
    uint32_t entrylo = (PPAGE_TO_PADDR(entry->ppn) & TLBLO_PPAGE) | TLBLO_VALID;

    if (!entry->readonly) {
        entrylo |= TLBLO_DIRTY;
    }

    bool holding_tlblock = spinlock_do_i_hold(&tlb_spinlock);

    if (!holding_tlblock) {
        spinlock_acquire(&tlb_spinlock);
    }

    int spl = splhigh();
    tlb_insert_entry(entryhi, entrylo);
    splx(spl);

    if (!holding_tlblock) {
        spinlock_release(&tlb_spinlock);
    }

    lock_release(as->as_lock);
    return 0;
}

vaddr_t alloc_kpages(unsigned npages) {
    while (true) {
        spinlock_acquire(&cm_spinlock);

        pp_num_t start = first_page;
        int result = find_free_pp(npages, &start);

        if (result == 0) {
            for (pp_num_t pp = start; pp < (pp_num_t)(start + npages); pp++) {
                kalloc_ppage(pp);
            }

            /* Mark end of block for kfree later */
            cm->entries[start + npages - 1].kmalloc_end = true;

            spinlock_release(&cm_spinlock);

            vaddr_t kvaddr = PADDR_TO_KVADDR(PPAGE_TO_PADDR(start));
            return kvaddr;
        }

        spinlock_release(&cm_spinlock);

        pp_num_t freed;
        int ev = evict_one(&freed);
        if (ev) {
            return 0;
        }
    }
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
    spinlock_acquire(&tlb_spinlock);

    int spl = splhigh();

    for (int i = 0; i < NUM_TLB; i++) {
        tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
    }

        splx(spl);
    spinlock_release(&tlb_spinlock);
}

void vm_tlbshootdown(const struct tlbshootdown *tlb) {
    spinlock_acquire(&tlb_spinlock);
    int spl = splhigh();

    int idx = tlb_probe(tlb->vaddr & TLBHI_VPAGE, 0);

    if (idx >= 0) {
        tlb_write(TLBHI_INVALID(idx), TLBLO_INVALID(), idx);
    }

    splx(spl);
    spinlock_release(&tlb_spinlock);
}
