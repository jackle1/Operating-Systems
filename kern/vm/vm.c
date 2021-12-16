#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <spinlock.h>
#include <proc.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>
#include <mips/vm.h>
#include <mainbus.h>

static struct spinlock coremap_splk = SPINLOCK_INITIALIZER;
static struct spinlock stealmem_lock = SPINLOCK_INITIALIZER;
static struct coremap_entry *coremap;
static int coremap_entries;
paddr_t firstpaddr;
paddr_t lastpaddr;

static
paddr_t
getppages(unsigned long npages)
{
	paddr_t addr;
    /* If coremap not initialized, steal memory from ram */
    if (!coremap) {
		spinlock_acquire(&stealmem_lock);
		addr = ram_stealmem(npages);
		spinlock_release(&stealmem_lock);
		return addr;
	}

	spinlock_acquire(&coremap_splk);

	int start = -1;
	unsigned pagesSoFar = 0;
    /* Find a contiguous block of free pages in coremap */
	for (int i = 0; i < coremap_entries; i++) {
		if (coremap[i].freecount == 0) {
			if (start == -1) {
				start = i;
			}
			pagesSoFar++;
		}
		else {
			start = -1;
			pagesSoFar = 0;
		}
		if (pagesSoFar == npages) {
			break;
		}
	}

	if (pagesSoFar != npages || start == -1) {
		spinlock_release(&coremap_splk);
		return 0;
	}

    /* Set values for block */
	for (unsigned i = start; i < npages + start; i++) {
		if (i == (unsigned)start) {
			coremap[i].block_start = 1;
		}
		if (i == npages + start - 1){
			coremap[i].block_end = 1;
		}
		coremap[i].freecount = 1;
	}

    spinlock_release(&coremap_splk);

	return firstpaddr + start * PAGE_SIZE;
}

vaddr_t
alloc_kpages(unsigned npages)
{
	paddr_t pa;
	pa = getppages(npages);
	if (pa == 0) {
		return 0;
	}
	return PADDR_TO_KVADDR(pa);
}

void
free_kpages(vaddr_t addr)
{
    /* Leaks memory if it is not included in coremap */
    if (PADDR_TO_KVADDR(firstpaddr) <= addr) {
        int freeBlock = 0;
        int start_index = (addr - PADDR_TO_KVADDR(firstpaddr)) / PAGE_SIZE;

    	spinlock_acquire(&coremap_splk);
    	for (int i = 0; i < coremap_entries; i++) {
    		if (i == start_index) {
    			freeBlock = 1;
                coremap[i].block_start = 0;
    		}
    		if (freeBlock) {
    			coremap[i].freecount = 0;
    			if (coremap[i].block_end == 1) {
    				freeBlock = 0;
                    coremap[i].block_end = 0;
    			}
    		}
    	}
    	spinlock_release(&coremap_splk);
    }
}

void
vm_bootstrap(void)
{
    firstpaddr = ram_getfirst();
	lastpaddr = ram_getsize() - 1;
	paddr_t size = lastpaddr - firstpaddr;
	int npages = size / PAGE_SIZE;
    spinlock_acquire(&coremap_splk);
	coremap = (struct coremap_entry*)PADDR_TO_KVADDR(firstpaddr);
    coremap_entries = npages;
    /* Coremap memory is initalized to free */
	for (int i = 0; i < npages; i++) {
        coremap[i].freecount = 0;
        coremap[i].block_start = 0;
        coremap[i].block_end = 0;
	}
    /* Steal memory from RAM for coremap */
    spinlock_acquire(&stealmem_lock);
    ram_stealmem(npages);
    spinlock_release(&stealmem_lock);
    spinlock_release(&coremap_splk);
}

void
vm_tlbshootdown_all(void)
{
	/* Unimplemented */
}

void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
	/* Unimplemented */
	(void) ts;
}

int
vm_fault(int faulttype, vaddr_t faultaddress)
{
	/* Unimplemented */
	(void) faulttype;
	(void) faultaddress;

    return 0;
}
