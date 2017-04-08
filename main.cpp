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

using namespace std;

void init_hypercalls(void *hypercall_page) {
  // model this on hypercall_page_initialise_ring1_kernel from xen
}

int main(int argc, char **argv) {
  assert(argc == 2);
  //void *unikernel_start;
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
  
  void *hypercall_page = mmap(0xa49000, 4096, PROT_READ|PROT_WRITE|PROT_EXEC, MAP_PRIVATE|MAP_FIXED|MAP_ANONYMOUS, -1, 0);
  init_hypercalls(hypercall_page);
  printf("entry point %p\n", ehdr.e_entry);
  void* raw_entry = reinterpret_cast<void*>(ehdr.e_entry);
  asm("call %P0" : : "m"(raw_entry));

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
