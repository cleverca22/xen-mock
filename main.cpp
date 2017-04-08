#include <stdint.h>
#include <dlfcn.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <iostream>
#include <libelf/libelf.h>
#include <libelf/gelf.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdlib.h>

using namespace std;

typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;
#define PAGE_SIZE 4096

//xen/include/public/vcpu.h:
#define VCPUOP_get_runstate_info     4

u64 do_vcpu_op(u64 cmd, u64 vcpu, void* extra){
    if (vcpu < 8) {
      return 0;
    } else {
      return -1;
    }
}
u64 hyper_test(u64 a1, void* a2, void* a3, void* a4, void* a5, u64 hypercall_op) {
  printf("op %p\n", (void*)hypercall_op);
  printf("in hypertest %p %p %p %p %p\n", a1, a2, a3, a4, a5);
  switch (a1) {
  case VCPUOP_get_runstate_info:
    return do_vcpu_op(a1, (u64)a2, a3);
  }
  exit(0);
}
void init_hypercalls(void *hypercall_page) {
  char *p;
  int i;
  memset(hypercall_page, 0x90, 4096);
  // modeled this on hypercall_page_initialise_ring1_kernel from xen
  for ( i = 0; i < (PAGE_SIZE / 32); i++ ) {
    //if ( i == __HYPERVISOR_iret ) continue;
    p = (char *)(hypercall_page + (i * 32));
    if (false) {
    *(u8  *)(p+ 0) = 0x4c;    /* mov  $<i>,%r9 */
    *(u8  *)(p+ 1) = 0x8b;
    *(u8  *)(p+ 2) = 0x0c;
    *(u8  *)(p+ 3) = 0x25;
    *(u32 *)(p+ 4) = i;
    }
    *(u8  *)(p+ 8) = 0x48;    /* movabs $<x>, %rax */
    *(u8  *)(p+ 9) = 0xb8;    /* %rax */
    *(u64 *)(p+10) = (u64)hyper_test;
    *(u8  *)(p+18) = 0xff;    /* call *%rax */
    *(u8  *)(p+19) = 0xd0;    /* %rax */
    *(u8  *)(p+20) = 0xc3;    /* ret */
  }
  FILE *fh = fopen("hypercall_page.bin","w");
  fwrite(hypercall_page, 1, 4096, fh);
  fclose(fh);
}

int main(int argc, char **argv) {
  assert(argc == 2);
  size_t n, shstrndx;

  if (elf_version(EV_CURRENT) == EV_NONE) {
    cerr << "internal libelf error\n";
    return -2;
  }

  int fd = open(argv[1], O_RDONLY);
  assert(fd != -1);

  Elf *unikernel = elf_begin(fd, ELF_C_READ, NULL);
  int err = elf_errno();
  if (err) {
    cerr << "got Elf* " << unikernel << " " << elf_errmsg(err) << "\n";
  }
  assert(unikernel);

  GElf_Ehdr ehdr;
  if (gelf_getehdr(unikernel, &ehdr) == NULL) {
    cerr << elf_errmsg(-1) << "\n";
    return -3;
  }

  if (elf_getphdrnum(unikernel, &n) != 0) {
    cerr << elf_errmsg(-1) << "\n";
    return -4;
  }
  cerr << "found " << n << " pheaders\n";

  GElf_Phdr phdr; // = elf64_getphdr(unikernel);
  for (size_t i=0; i<n; i++) {
    cerr << "phdr#" << i << "\n";
    if (gelf_getphdr(unikernel, i, &phdr) != &phdr) {
      cerr << elf_errmsg(-1) << "\n";
      return -5;
    }
#define PRINT_FIELD(x) do { printf("%s: %p\n", #x, reinterpret_cast<void*>(phdr.x)); } while (0)
    PRINT_FIELD(p_type);
    PRINT_FIELD(p_vaddr); // virt address to load thing to
    PRINT_FIELD(p_paddr); // phys addr ^^
    PRINT_FIELD(p_filesz); // size in ELF file
    PRINT_FIELD(p_memsz); // size in ram
    PRINT_FIELD(p_offset); // file offset to this thing
    PRINT_FIELD(p_flags);
    PRINT_FIELD(p_align);
    if (phdr.p_type == PT_LOAD) {
      cerr << "found PT_LOAD\n";
      int prot = 0;
      if (phdr.p_flags & PF_X) prot |= PROT_EXEC;
      if (phdr.p_flags & PF_W) prot |= PROT_WRITE;
      if (phdr.p_flags & PF_R) prot |= PROT_READ;
      void *rawaddr;
      rawaddr = mmap(reinterpret_cast<void*>(phdr.p_vaddr), phdr.p_memsz, prot, MAP_PRIVATE|MAP_FIXED|MAP_ANONYMOUS, -1, 0);
      assert(rawaddr == reinterpret_cast<void*>(phdr.p_vaddr));
      int sz = pread(fd, rawaddr, phdr.p_filesz, phdr.p_offset);
      assert(sz == phdr.p_filesz);
    }
  }
  if ( elf_getshdrstrndx (unikernel , & shstrndx ) != 0) {
    cerr << elf_errmsg(err) << "\n";
    return -5;
  }
  //cerr << "section header string index " << shstrndx << "\n";
  
  void *hypercall_page = mmap((void*)0xa49000, 4096, PROT_READ|PROT_WRITE|PROT_EXEC, MAP_PRIVATE|MAP_FIXED|MAP_ANONYMOUS, -1, 0);
  init_hypercalls(hypercall_page);
  printf("entry point %p\n", ehdr.e_entry);
  void* raw_entry = reinterpret_cast<void*>(ehdr.e_entry);
  asm("call %P0" : : "rm"(raw_entry));

  /*Elf_Scn *scn = NULL;
  GElf_Shdr shdr;
  int i = 0;
  while ((scn = elf_nextscn(unikernel, scn)) != NULL) {
    if (gelf_getshdr(scn, &shdr) != &shdr) {
      cerr << elf_errmsg(err) << "\n";
      return -6;
    }
    char *name;
    if ((name = elf_strptr(unikernel, shstrndx, shdr.sh_name)) == NULL) {
      cerr << elf_errmsg(err) << "\n";
      return -7;
    }
    if (shdr.sh_type == SHT_SYMTAB) {
      cerr << "found symbol table\n";
      process_symbol_table(shdr, 
    }
    cerr << "section name#" << ++i <<  ": " << name << "\n";
  }*/

  return 0;
}
