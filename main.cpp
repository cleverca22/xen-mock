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
#include <signal.h>

#define __XEN_INTERFACE_VERSION__ 0x00040500

#include <xen/xen.h>
#include <xen/memory.h>
#include <xen/vcpu.h>
#include <xen/sched.h>

using namespace std;

typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;
#define PAGE_SIZE 4096

int memory = 64;
int page_count = memory * 256;

u64 do_vcpu_op(u64 cmd, u64 vcpu, void* extra){
  switch (cmd) {
  case VCPUOP_get_runstate_info:
    if (vcpu < 1) {
      return 0;
    } else {
      return -1;
    }
  }
  return -EINVAL;
}

u64 do_memory_op(u64 cmd, u64 *domid) {
  switch (cmd) {
  case XENMEM_current_reservation:
    return page_count;
  case XENMEM_maximum_reservation:
    return page_count;
  default:
    exit(2);
  }
}

u64 do_sched_op(u64 cmd, void *a2) {
  switch (cmd) {
  case SCHEDOP_shutdown:
    printf("\"clean\" shutdown\n");
    exit(0);
  default:
    exit(2);
  }
}

void handler(int, siginfo_t *, void *) {
  puts("segfault!");
  _exit(-1);
}

u64 hyper_test(u64 a1, void* a2, void* a3, void* a4, void* a5, u64 hypercall_op) {
  switch (hypercall_op) {
  case __HYPERVISOR_memory_op:
    printf("op memory_op\n");
    printf("in hypertest %p %p %p %p %p\n", a1, a2, a3, a4, a5);
    return do_memory_op((u64)a1, (u64*)a2);
  case 0x12:
    printf("du: %s", (char*)a3);
    return 0;
  case __HYPERVISOR_vcpu_op: // 24
    return do_vcpu_op(a1, (u64)a2, a3);
  case __HYPERVISOR_sched_op: // 29
    return do_sched_op((u64)a1, a2);
  }
  printf("op %p\n", (void*)hypercall_op);
  printf("in hypertest %p %p %p %p %p\n", a1, a2, a3, a4, a5);
  exit(0);
}
void init_hypercalls(void *hypercall_page) {
  char *p;
  int i;
  memset(hypercall_page, 0x90, 4096);
  // modeled this on hypercall_page_initialise_ring1_kernel from xen
  for ( i = 0; i < (PAGE_SIZE / 32); i++) {
    //if ( i == __HYPERVISOR_iret ) continue;
    p = (char *)(hypercall_page + (i * 32));
    if (true) {
    *(u8  *)(p+ 0) = 0x49;    /* mov  $<i>,%r9 */
    *(u8  *)(p+ 1) = 0xc7;
    *(u8  *)(p+ 2) = 0xc1;
    *(u32 *)(p+ 3) = i;
    }
    *(u8  *)(p+ 7) = 0x48;    /* movabs $<x>, %rax */
    *(u8  *)(p+ 8) = 0xb8;    /* %rax */
    *(u64 *)(p+ 9) = (u64)hyper_test;
    *(u8  *)(p+17) = 0xff;    /* call *%rax */
    *(u8  *)(p+18) = 0xd0;    /* %rax */
    *(u8  *)(p+19) = 0xc3;    /* ret */
  }
  FILE *fh = fopen("hypercall_page.bin","w");
  fwrite(hypercall_page, 1, 4096, fh);
  fclose(fh);
  printf("setup hypercalls at address %p\n", hypercall_page);
}

int test_findme() {
  asm("mov $0x11223344, %r9");
  return 0;
}

struct entry_args {
  void *raw_entry;
  start_info_t *start_info;
};
void *entry_wrap(void *a1) {
  struct entry_args *args = reinterpret_cast<struct entry_args*>(a1);
  register unsigned long __res asm("rax");
  register start_info_t * start_info asm("rsi") = start_info;
  start_info = args->start_info;
  asm("call %P[entry]" : "+r"(start_info), "=r"(__res) : [entry] "rm"(args->raw_entry) : );
  printf("unikernel returned %d\n", __res);
}
void do_unikernel(void *raw_entry, start_info_t *si, void *guest_heap) {
  struct sigaction sa;
  sa.sa_flags = SA_SIGINFO;
  sigemptyset(&sa.sa_mask);
  sa.sa_sigaction = handler;
  //int ret = sigaction(SIGSEGV, &sa, NULL);
  //assert(ret != -1);
  
  void *stack_start = guest_heap;
  void *stack_end = guest_heap + (2 * 1024 * 1024);

  pthread_attr_t attrs;
  pthread_t thread;

  pthread_attr_init(&attrs);
  pthread_attr_setstackaddr(&attrs, stack_end);
  printf("stack should span %p to %p\n", stack_start, stack_end);

  struct entry_args args;
  args.raw_entry = raw_entry;
  args.start_info = si;

  pthread_create(&thread, &attrs, entry_wrap, &args);

  pthread_attr_destroy(&attrs);

  printf("blocking until thread returns\n");
  pthread_join(thread, NULL);
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
  //cerr << "found " << n << " pheaders\n";

  GElf_Phdr phdr;
  for (size_t i=0; i<n; i++) {
    //cerr << "phdr#" << i << "\n";
    if (gelf_getphdr(unikernel, i, &phdr) != &phdr) {
      cerr << elf_errmsg(-1) << "\n";
      return -5;
    }
#define PRINT_FIELD(x) do { printf("%s: %p\n", #x, reinterpret_cast<void*>(phdr.x)); } while (0)
    //PRINT_FIELD(p_type);
    //PRINT_FIELD(p_vaddr); // virt address to load thing to
    //PRINT_FIELD(p_paddr); // phys addr ^^
    //PRINT_FIELD(p_filesz); // size in ELF file
    //PRINT_FIELD(p_memsz); // size in ram
    //PRINT_FIELD(p_offset); // file offset to this thing
    //PRINT_FIELD(p_flags);
    //PRINT_FIELD(p_align);
    if (phdr.p_type == PT_LOAD) {
      //cerr << "found PT_LOAD\n";
      int prot = 0;
      if (phdr.p_flags & PF_X) prot |= PROT_EXEC;
      if (phdr.p_flags & PF_W) prot |= PROT_WRITE;
      if (phdr.p_flags & PF_R) prot |= PROT_READ;
      void *rawaddr;
      rawaddr = mmap(reinterpret_cast<void*>(phdr.p_vaddr), phdr.p_memsz, prot, MAP_PRIVATE|MAP_FIXED|MAP_ANONYMOUS, -1, 0);
      assert(rawaddr == reinterpret_cast<void*>(phdr.p_vaddr));
      int sz = pread(fd, rawaddr, phdr.p_filesz, phdr.p_offset);
      assert(sz == phdr.p_filesz);
      printf("\033[31m%p len=%x == ELF\033[00m\n", phdr.p_vaddr, phdr.p_memsz);
    }
  }
  if ( elf_getshdrstrndx (unikernel , & shstrndx ) != 0) {
    cerr << elf_errmsg(err) << "\n";
    return -5;
  }
  //cerr << "section header string index " << shstrndx << "\n";
  
  void *hypercall_page = mmap((void*)0xa8b000, 4096, PROT_READ|PROT_WRITE|PROT_EXEC, MAP_PRIVATE|MAP_FIXED|MAP_ANONYMOUS, -1, 0);
  init_hypercalls(hypercall_page);
  printf("\033[31m%p len=%x == hypercall_page\033[00m\n", hypercall_page, 4096);
  printf("entry point %p\n", ehdr.e_entry);
  void* raw_entry = reinterpret_cast<void*>(ehdr.e_entry);
  start_info_t start_info;
  memset(&start_info, 0, sizeof(start_info));

  void *guest_heap = mmap(0, memory * 1024 * 1024, PROT_READ|PROT_WRITE|PROT_EXEC, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
  assert(guest_heap);
  printf("\033[31m%p len=%x == guest heap\033[00m\n", guest_heap, memory * 1024 * 1024);
  uint64_t guest_start = (uint64_t)(guest_heap) / 4096;
  uint64_t guest_end = guest_start + page_count;
  printf("guest heap page range %lu-%lu, %lu entries\n", guest_start, guest_end, guest_end - guest_start);
  uint64_t *mfn_list = new uint64_t[guest_end - guest_start];
  for (uint64_t i = guest_start, j=0; i < guest_end; i++, j++) {
    mfn_list[j] = i;
  }

  start_info.mfn_list = reinterpret_cast<uint64_t>(mfn_list);
  do_unikernel(raw_entry, &start_info, guest_heap);

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
