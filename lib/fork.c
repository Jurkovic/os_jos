// implement fork from user space

#include <inc/string.h>
#include <inc/lib.h>

// PTE_COW marks copy-on-write page table entries.
// It is one of the bits explicitly allocated to user processes (PTE_AVAIL).
#define PTE_COW		0x800

//
// Custom page fault handler - if faulting page is copy-on-write,
// map in our own private writable copy.
//
static void
pgfault(struct UTrapframe *utf)
{
	void *addr = (void *) utf->utf_fault_va;
	uint32_t err = utf->utf_err;
	int r;

	// Check that the faulting access was (1) a write, and (2) to a
	// copy-on-write page.  If not, panic.
	// Hint:
	//   Use the read-only page table mappings at uvpt
	//   (see <inc/memlayout.h>).

	// LAB 4: Your code here.
	if(!(err & FEC_WR)) {
		panic("pgfault: fault access nieje write");
	}
	
	pte_t pte = uvpt[PGNUM(addr)];
	if((pte & (PTE_U | PTE_P)) != (PTE_U | PTE_P)) {
		panic("pgfault: niesu nastavene potrebne pravomoci stranky");
	}	

	if(!(pte & PTE_COW)) {
		panic("pgfault: nieje nastaveny bit PTE_COW");
	}

	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.

	// LAB 4: Your code here.
	
	r = sys_page_alloc(0, (void*)PFTEMP, PTE_U | PTE_P | PTE_W);
	if(r < 0)
		panic("pgfault: pri alokacii stranky doslo k chybe %e", r);

	memmove((void*)PFTEMP, ROUNDDOWN(addr,PGSIZE), PGSIZE);
	
	r = sys_page_map(0, (void*)PFTEMP, 0, ROUNDDOWN(addr,PGSIZE), PTE_P | PTE_W | PTE_U);
    if(r < 0)
        panic("pgfault: pri mapovani stranky doslo k chybe %e", r);

	r = sys_page_unmap(0, (void*)PFTEMP);
        if(r < 0)
                panic("pgfault: pri odmapovani stranky doslo k chybe %e", r);

}

///
// Map our virtual page pn (address pn*PGSIZE) into the target envid
// at the same virtual address.  If the page is writable or copy-on-write,
// the new mapping must be created copy-on-write, and then our mapping must be
// marked copy-on-write as well.  (Exercise: Why do we need to mark ours
// copy-on-write again if it was already copy-on-write at the beginning of
// this function?)
//
// Returns: 0 on success, < 0 on error.
// It is also OK to panic on error.
//
static int
duppage(envid_t envid, unsigned pn)
{
	int r;

	// LAB 4: Your code here.
		
	void* va = (void*)(pn*PGSIZE);
	pte_t pte = uvpt[pn];	

	if(pte & PTE_SHARE) {
		r = sys_page_map(0,va,envid,va, pte & PTE_SYSCALL);
		if(r < 0)
			panic("duppage: vyskytla sa chyba pri page_map read only %e", r);
	}

	if(pte & (PTE_COW | PTE_W)) {

		r = sys_page_map(0, va, envid, va, PTE_COW | PTE_U | PTE_P);
		if(r < 0) {
			panic("duppage: vyskytla sa chyba pri child page_map %e", r);
		}
		r = sys_page_map(0, va, 0, va, PTE_COW | PTE_U | PTE_P);
        if(r < 0) {
            panic("duppage: vyskytla sa chyba pri rodicovi page_map %e", r);
        }
	}
	return 0;
}

//
// User-level fork with copy-on-write.
// Set up our page fault handler appropriately.
// Create a child.
// Copy our address space and page fault handler setup to the child.
// Then mark the child as runnable and return.
//
// Returns: child's envid to the parent, 0 to the child, < 0 on error.
// It is also OK to panic on error.
//
// Hint:
//   Use uvpd, uvpt, and duppage.
//   Remember to fix "thisenv" in the child process.
//   Neither user exception stack should ever be marked copy-on-write,
//   so you must allocate a new page for the child's user exception stack.
//
envid_t
fork(void)
{
	// LAB 4: Your code here.
	set_pgfault_handler(pgfault); //nastavenie handlera preruseni
	envid_t id;
	int r;

	id = sys_exofork(); //vytvorenie child env
	if(id < 0) {
		panic("fork: chyba pri exofork %e", id);
		return id;
	}
	if(id == 0) {
		//toto je child proces
		thisenv = &envs[ENVX(sys_getenvid())];
		return 0;
	}
	//toto je parent proces
	uint8_t* addr;
	for(addr = 0; addr < (uint8_t*)USTACKTOP; addr += PGSIZE) {

		if((uvpd[PDX(addr)] & PTE_P) && (uvpt[PGNUM(addr)] & PTE_P)) {
			r = duppage(id, PGNUM(addr));
			if(r < 0) {
				panic("fork: chyba pri duppage %e", r);			
			}
		}	
	}
	r = sys_page_alloc(id, (void *)(UXSTACKTOP-PGSIZE), PTE_P | PTE_U | PTE_W);
    	if(r < 0) {
            panic("fork: chyba pri page_alloc %e", r);
	}

	r = sys_env_set_pgfault_upcall(id, thisenv->env_pgfault_upcall);
	if(r < 0) {
		panic("fork: chyba pri env_set_pgfault_upcall %e", r);
	}
        // Start the child environment running
    if ((r = sys_env_set_status(id, ENV_RUNNABLE)) < 0) {
        panic("sys_env_set_status: %e", r);
	}

    return id;	
}

// Challenge!
int
sfork(void)
{
	panic("sfork not implemented");
	return -E_INVAL;
}
