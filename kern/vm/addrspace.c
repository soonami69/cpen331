/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <addrspace.h>
#include <proc.h>
#include <vm.h>

/*
 * Note! If OPT_DUMBVM is set, as is the case until you start the VM
 * assignment, this file is not compiled or linked or in any way
 * used. The cheesy hack versions in dumbvm.c are used instead.
 */

struct addrspace *
as_create(void)
{
	struct addrspace *as;

	as = kmalloc(sizeof(struct addrspace));
	if (as == NULL) {
		return NULL;
	}

	/* Initialize pagetable. */
    as->pt = pagetable_create();
	if (!as->pt) {
        kfree(as);
        return NULL;
    }

    as->region_list = NULL;

	/* Heap should be empty and grows from 0 */
    as->heap_start = 0;
    as->heap_end = 0;

    as->stack_base = USERSTACK;

    return as;
}

static int region_copy(struct region *src, struct region **ret) {
    struct region* new_head = NULL;
    struct region* prev = NULL;

	while(src) {
		struct region *r = kmalloc(sizeof(struct region));

		if (!r) {
			/* Free all we've made thus far */
			struct region *tmp = new_head;
			while (tmp) {
				struct region *next = tmp->next;
                kfree(tmp);
                tmp = next;
            }
            return ENOMEM;
        }

		/* copy the region fields */
        r->as_vbase = src->as_vbase;
        r->as_npages = src->as_npages;
        r->read = src->read;
        r->write = src->write;
        r->exec = src->exec;
        r->next = NULL;

		if (prev) {
            prev->next = r;
        } else {
            new_head = r;
        }

        prev = r;
        src = src->next;
    }

    *ret = new_head;
    return 0;
}

static void region_destroy(struct region *r) {
    struct region *curr = r;

	while (curr) {
        struct region *next = curr->next;
        kfree(curr);
        curr = next;
    }
}

int
as_copy(struct addrspace *old, struct addrspace **ret)
{
	struct addrspace *newas;

	newas = as_create();
	if (newas==NULL) {
		return ENOMEM;
	}

    int err;

    err = pagetable_copy(old->pt, &newas->pt);

	if (err) {
        as_destroy(newas);
        return err;
    }

    err = region_copy(old->region_list, &newas->region_list);

	if (err) {
        as_destroy(newas);
        return err;
    }

    newas->heap_start = old->heap_start;
    newas->heap_end = old->heap_end;

    newas->stack_base = old->stack_base;

    *ret = newas;
	return 0;
}

void
as_destroy(struct addrspace *as)
{
	/*
	 * Clean up as needed.
	 */

    region_destroy(as->region_list);
    pagetable_destroy(as->pt);

    kfree(as);
}

void
as_activate(void)
{
	struct addrspace *as;

	as = proc_getas();
	if (as == NULL) {
		/*
		 * Kernel thread without an address space; leave the
		 * prior address space in place.
		 */
		return;
	}

    /*
     * Write this. ROY IMPLEMENT THIS <-------
     */
}

void
as_deactivate(void)
{
	/*
	 * Write this. For many designs it won't need to actually do
	 * anything. See proc.c for an explanation of why it (might)
	 * be needed.
	 */
}

/*
 * Set up a segment at virtual address VADDR of size MEMSIZE. The
 * segment in memory extends from VADDR up to (but not including)
 * VADDR+MEMSIZE.
 *
 * The READABLE, WRITEABLE, and EXECUTABLE flags are set if read,
 * write, or execute permission should be set on the segment. At the
 * moment, these are ignored. When you write the VM system, you may
 * want to implement them.
 */
int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t sz,
		 int readable, int writeable, int executable)
{
	/* we want to round the npages up */
    size_t npages = (sz + PAGE_SIZE - 1) / PAGE_SIZE;
    
	/* round down base to get a nice boundary */
	vaddr_t vbase = vaddr & PAGE_FRAME;

    struct region* r = kmalloc(sizeof(struct region));
	if (!r) {
        return ENOMEM;
    }

    r->as_vbase = vbase;
    r->as_npages = npages;
    r->read = readable;
    r->write = writeable;
    r->exec = executable;
    r->next = NULL;

    if (as->region_list == NULL) {
        as->region_list = r;
    } else {
        struct region* curr = as->region_list;
        while (curr->next) {
            curr = curr->next;
        }
        curr->next = r;
    }

    return 0;
}

int
as_prepare_load(struct addrspace *as)
{
	/* This function turns writing on for all regions */
    struct region* r = as->region_list;
    while (r) {
        r->write = 1;
        r = r->next;
    }
    return 0;
}

int
as_complete_load(struct addrspace *as)
{
    struct region* r = as->region_list;
    while (r) {
        /* only text segment which was exec should not be writable */
        if (r->exec && !r->write) {
            r->write = 0;
        }

        // Other regions like data/BSS are already correct
        r = r->next;
    }

    return 0;
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	/*
	 * Write this.
	 */

	(void)as;

	/* Initial user-level stack pointer */
	*stackptr = USERSTACK;

	return 0;
}

