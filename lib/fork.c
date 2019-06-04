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
	if(!(err&FEC_WR)) panic("pgfault: not a write\n");
	uintptr_t pteaddr = UVPT|(PGNUM(addr)<<2);
	int perm = (*(pte_t*)pteaddr)&PTE_SYSCALL;
	if(!(perm&PTE_COW)) panic("pgfault: not CoW\n");

	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.
	//cprintf("[%x] cow %08x from %08x ", sys_getenvid(), ROUNDDOWN(addr, PGSIZE), uvpt[PGNUM(addr)]);
	// LAB 4: Your code here.
	perm = (perm&(~PTE_COW)) | PTE_W;
	if((r = sys_page_alloc(0,(void*)PFTEMP,perm))<0) panic("pgfault: sys_page_alloc %e\n", r);
	memmove((void*)PFTEMP,ROUNDDOWN(addr,PGSIZE),PGSIZE);
	if((r = sys_page_map(0,(void*)PFTEMP,0,ROUNDDOWN(addr,PGSIZE),perm))<0) panic("pgfault: sys_page_map %e\n",r);
	if((r = sys_page_unmap(0,PFTEMP))<0) panic("pgfault: sys_page_unmap %e\n",r);
	//cprintf("to %08x\n", uvpt[PGNUM(addr)]);

}

//
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
	uintptr_t pteaddr = UVPT|(pn<<2);
	uintptr_t pdeaddr = UVPT|(PGNUM(pteaddr)<<2);
	int pdeperm = (*(pde_t*)pdeaddr)&PTE_SYSCALL;
	if(!(pdeperm&PTE_P)) return 0;
	int perm = (*(pte_t*)pteaddr)&PTE_SYSCALL;
	if(!(perm&PTE_P)) return 0;
	if(perm & PTE_W || perm& PTE_COW){
		perm = (perm&(~PTE_W))|PTE_COW;
		r = sys_page_map(0, (void*)(pn<<PTXSHIFT),envid,(void*)(pn<<PTXSHIFT),perm);
		if(r) panic("duppage: sys_page_map %e\n",r);
		r = sys_page_map(0, (void*)(pn<<PTXSHIFT),0,(void*)(pn<<PTXSHIFT),perm);
		if(r) panic("duppage: sys_page_map %e\n",r);
	} else {
		r = sys_page_map(0, (void*)(pn<<PTXSHIFT),envid,(void*)(pn<<PTXSHIFT),perm);
		if(r) panic("duppage: sys_page_map %e\n",r);
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
	set_pgfault_handler(pgfault);
	envid_t envid=sys_exofork();
	if(envid<0) panic("fork: sys_exofork %e\n",envid);
	if(envid==0){
		thisenv=&envs[ENVX(sys_getenvid())];
		return 0;
	}
	for(unsigned pn = 0; pn<PGNUM(UXSTACKTOP-PGSIZE);pn++)
		duppage(envid,pn);

    extern void _pgfault_upcall(void);
	int r = sys_page_alloc(envid,(void*)(UXSTACKTOP-PGSIZE),PTE_W|PTE_U|PTE_P);
	if(r) panic("fork: sys_page_alloc %e\n", r);
	sys_env_set_pgfault_upcall(envid,_pgfault_upcall);
	
	if ((r = sys_env_set_status(envid, ENV_RUNNABLE)) < 0)
		panic("fork: sys_env_set_status %e\n", r);

	return envid;
}

// Challenge!
int
sfork(void)
{
	panic("sfork not implemented");
	return -E_INVAL;
}
