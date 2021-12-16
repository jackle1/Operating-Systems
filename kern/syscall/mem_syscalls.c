#include <types.h>
#include <lib.h>
#include <array.h>
#include <syscall.h>
#include <current.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <limits.h>
#include <filetable.h>
#include <addrspace.h>
#include <mips/trapframe.h>
#include <kern/wait.h>
#include <vm.h>
#include <mips/vm.h>

int sys_sbrk(intptr_t amount, void *retval) {

    /* Unimplemented */
    (void) amount;
    (void) retval;
    return 0;
}
