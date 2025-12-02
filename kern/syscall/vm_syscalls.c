#include <types.h>
#include <addrspace.h>
#include <syscall.h>
#include <current.h>
#include <kern/errno.h>
#include <proc.h>
#include <cpu.h>
#include <vm.h>
#include <pagetable.h>

static void free_heap_pages(struct addrspace *as, vaddr_t new_end, vaddr_t old_end) {
    KASSERT(lock_do_i_hold(as->as_lock));
    KASSERT(new_end < old_end);

    vaddr_t start_page = ROUNDUP(new_end, PAGE_SIZE);
    vaddr_t end_page = ROUNDUP(old_end, PAGE_SIZE);

    for (vaddr_t vaddr = start_page; vaddr < end_page; vaddr += PAGE_SIZE) {
        struct pte* entry = pagetable_lookup(as->pt, vaddr);

        /* Skip this page if it is not valid or doesn't exist */
        if (entry == NULL || !entry->valid) {
            continue;
        }

        if (entry->in_mem) {
            paddr_t paddr = PPAGE_TO_PADDR(entry->ppn);
            free_kpages(PADDR_TO_KVADDR(paddr));
        }

        entry->valid = false;
        entry->in_mem = false;

        struct tlbshootdown tlb;
        tlb.vaddr = vaddr;
        vm_tlbshootdown(&tlb);
    }
}

int sys_sbrk(ssize_t amount, int *retval) {
    struct addrspace *as = proc_getas();

    if (as == NULL) {
        return EFAULT;
    }

    lock_acquire(as->as_lock);

    vaddr_t old_heap_end = as->heap_end;
    vaddr_t new_heap_end = old_heap_end + amount;

    if (amount == 0) {
        *retval = (int)old_heap_end;
        lock_release(as->as_lock);
        return 0;
    }

    if (amount < 0) {
        if (new_heap_end < as->heap_start) {
            lock_release(as->as_lock);
            return EINVAL;
        }

        free_heap_pages(as, new_heap_end, old_heap_end);

        as->heap_end = new_heap_end;
        *retval = (int)old_heap_end;
        lock_release(as->as_lock);
        return 0;
    }

    /* Round up to page boundary to check for collisions */
    vaddr_t new_heap_top = ROUNDUP(new_heap_end, PAGE_SIZE);

    /* Check if heap collides with stack */
    if (new_heap_top >= as->stack_base) {
        lock_release(as->as_lock);
        return ENOMEM;
    }

    as->heap_end = new_heap_end;
    *retval = (int)old_heap_end;

    lock_release(as->as_lock);
    return 0;
}