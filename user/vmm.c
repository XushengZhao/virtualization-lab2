#include <inc/lib.h>
#include <inc/vmx.h>
#include <inc/elf.h>
#include <inc/ept.h>
#include <inc/stdio.h>

#define GUEST_KERN "/vmm/kernel"
#define GUEST_BOOT "/vmm/boot"

#define JOS_ENTRY 0x7000

// Map a region of file fd into the guest at guest physical address gpa.
// The file region to map should start at fileoffset and be length filesz.
// The region to map in the guest should be memsz.  The region can span multiple pages.
//
// Return 0 on success, <0 on failure.
//
// Hint: Call sys_ept_map() for mapping page. 
static int
map_in_guest( envid_t guest, uintptr_t gpa, size_t memsz, 
	      int fd, size_t filesz, off_t fileoffset ) {
	int r;
	/* Your code here */
	

	//allocate memory into the current env
	if ((r = sys_page_alloc(0,UTEMP,PTE_P|PTE_W|PTE_U)) < 0) return r;
	//seek file offset
	if ((r = seek(fd,fileoffset)) < 0) return r;
	//loop through the program segment one PGSIZE until filesz
	int loop = filesz;
	for (;loop> 0; loop -= PGSIZE){
		int readsize = loop >= PGSIZE ? PGSIZE: loop;
		if (read(fd,UTEMP,readsize) < readsize) return -1;
		//allocate memory into the guest environment
		void * curr_gpa = (void*)(gpa + (filesz - loop));
		if (( r = sys_ept_map(0,UTEMP,guest,curr_gpa,PTE_P|PTE_W|PTE_U)) < 0) {
			//cprintf("Failed to map page for guest\n");
			return r;
		}
	}
	
	return 0;
} 

// Read the ELF headers of kernel file specified by fname,
// mapping all valid segments into guest physical memory as appropriate.
//
// Return 0 on success, <0 on error
//
// Hint: compare with ELF parsing in env.c, and use map_in_guest for each segment.
static int
copy_guest_kern_gpa( envid_t guest, char* fname ) {
	/* Your code here */
	struct Elf elf;
	struct Env * guestenv;
	struct Proghdr ph;
	struct Secthdr sh;
	int r;
	//get guestenv
	//if ((r = envid2env(guest,&guestenv,true)) < 0) return r;

	//open file check for errors
	int fd = open(fname,O_RDONLY);
	if (fd < 0) return fd;

	//read elf header
	if (read(fd,&elf,sizeof(elf) < sizeof(elf))) return -1;
	if (elf.e_magic == ELF_MAGIC) {
		//lcr3(PADDR((uint64_t) guestenv->env_pml4e));
		//read program headers
		int i =0;
		for (;i<elf.e_phnum;i++){
			//seek first
			if ((r = seek(fd,elf.e_phoff + (i * sizeof(ph)))) < 0) return r;
			if (read(fd,&ph,sizeof(ph) < sizeof(ph))) return -1;
			//map in the program header
			if (ph.p_type == ELF_PROG_LOAD){
				if (( r = map_in_guest(guest,ph.p_va,ph.p_memsz,fd,ph.p_filesz,ph.p_offset)) < 0) return r;
			}
		}
	}
	return 0;
}

void
umain(int argc, char **argv) {
	int ret;
	envid_t guest;
	char filename_buffer[50];	//buffer to save the path 
	int vmdisk_number;
	int r;
	if ((ret = sys_env_mkguest( GUEST_MEM_SZ, JOS_ENTRY )) < 0) {
		cprintf("Error creating a guest OS env: %e\n", ret );
		exit();
	}
	guest = ret;

	// Copy the guest kernel code into guest phys mem.
	if((ret = copy_guest_kern_gpa(guest, GUEST_KERN)) < 0) {
		cprintf("Error copying page into the guest - %d\n.", ret);
		exit();
	}

	// Now copy the bootloader.
	int fd;
	if ((fd = open( GUEST_BOOT, O_RDONLY)) < 0 ) {
		cprintf("open %s for read: %e\n", GUEST_BOOT, fd );
		exit();
	}

	// sizeof(bootloader) < 512.
	if ((ret = map_in_guest(guest, JOS_ENTRY, 512, fd, 512, 0)) < 0) {
		cprintf("Error mapping bootloader into the guest - %d\n.", ret);
		exit();
	}
#ifndef VMM_GUEST	
	sys_vmx_incr_vmdisk_number();	//increase the vmdisk number
	//create a new guest disk image
	
	vmdisk_number = sys_vmx_get_vmdisk_number();
	snprintf(filename_buffer, 50, "/vmm/fs%d.img", vmdisk_number);
	
	cprintf("Creating a new virtual HDD at /vmm/fs%d.img\n", vmdisk_number);
        r = copy("vmm/clean-fs.img", filename_buffer);
        
        if (r < 0) {
        	cprintf("Create new virtual HDD failed: %e\n", r);
        	exit();
        }
        
        cprintf("Create VHD finished\n");
#endif
	// Mark the guest as runnable.
	sys_env_set_status(guest, ENV_RUNNABLE);
	wait(guest);
}


