#include <types.h>
#include <lib.h>
#include <array.h>
#include <copyinout.h>
#include <syscall.h>
#include <current.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <limits.h>
#include <proc.h>
#include <proctable.h>
#include <filetable.h>
#include <addrspace.h>
#include <mips/trapframe.h>
#include <kern/wait.h>

int sys_fork(struct trapframe *tf, pid_t *retval) {
    int result = 0;

    lock_acquire(proctable->lock);
    lock_acquire(curproc->lock);
    pid_t curpid = proctable->pid;
    proctable->pid++;

    proctable->proc[curpid] = kmalloc(sizeof(struct proc));
    if (!proctable->proc[curpid]) {
        lock_release(proctable->lock);
        lock_release(curproc->lock);
        return ENOMEM;
    }

    /* copy address space */
    result = as_copy(curproc->p_addrspace, &proctable->proc[curpid]->p_addrspace);
    if (result) {
        lock_release(curproc->lock);
        lock_release(proctable->lock);
        return result;
    }
    as_activate();

    /* copy filetable */
    result = filetable_copy(curproc, proctable->proc[curpid]);
    if (result) {
        lock_release(proctable->lock);
        lock_release(curproc->lock);
        return result;
    }

    /* Set name, VFS and proctable fields */
    spinlock_init(&proctable->proc[curpid]->p_lock);
    proctable->proc[curpid]->p_name = kstrdup(curproc->p_name);
    if (curproc->p_cwd != NULL) {
		VOP_INCREF(curproc->p_cwd);
		proctable->proc[curpid]->p_cwd = curproc->p_cwd;
	}
    proctable->proc[curpid]->pid = curpid;
    proctable->proc[curpid]->lock = lock_create("");
    proctable->proc[curpid]->exit_signal = cv_create("");
    proctable->proc[curpid]->exited = 0;
    proctable->proc[curpid]->parent_pid = curproc->pid;

    /* Copy, tweak trapframe and copy kernel thread */
    struct trapframe * tf_new = kmalloc(sizeof (struct trapframe));
    memcpy(tf_new, tf, sizeof(struct trapframe));
    result = thread_fork("", proctable->proc[curpid], enter_forked_process, tf_new, 1);
    if (result) {
        lock_release(proctable->lock);
        lock_release(curproc->lock);
        return result;
    }

    lock_release(proctable->lock);
    lock_release(curproc->lock);

    *retval = curpid;
    return result;
}

/* Check if string length is smaller than ARG_MAX */
static int length_check(const char *string, int max, size_t *actual_length) {
	int result;
	int i = 0;
	char next_char;

	do {
		result = copyin((const_userptr_t) &string[i], (void *) &next_char, (size_t) sizeof(char));
		if (result) {
			return result;
		}

	} while (next_char != 0 && i < max);

	if (next_char != 0) {
		return E2BIG;
	}

	*actual_length = i;
	return 0;
}

/*Copies arguments from the old address space*/
static int copy_from_old(int argc, char **args, char **args_in, int *size) {
	int arg_size_left = ARG_MAX;
	size_t cur_size;
	int result;

	for (int i = 0; i < argc; i++) {
		result = length_check((const char *) args[i], arg_size_left, &cur_size);
		if (result) {
			for (int j = 0; j < i; j++) {
				kfree(args_in[j]);
			}
			return result;
		}

		arg_size_left -= (cur_size + 1);
		size[i] = (int) cur_size + 1;

        size_t *path_len = kmalloc(sizeof(int));
        result = copyinstr((const_userptr_t) args[i], args_in[i], (cur_size + 1), path_len);
            if (result) {
		        kfree(*args_in);
		        kfree(path_len);
		        return result;
	        }

	}

	return 0;
}

/* Copies args to the new address space */
static int copy_to_new(int argc, char **args, int *size, vaddr_t *stackptr, userptr_t *argsv_addr) {
    int result;
	userptr_t arg_addr = (userptr_t) (*stackptr - argc*sizeof(userptr_t *) - sizeof(NULL));
	userptr_t *args_copied = (userptr_t *) (*stackptr - argc*sizeof(userptr_t *) - sizeof(NULL));

	for (int i = 0; i < argc; i++) {
		arg_addr -= size[i];
		*args_copied = arg_addr;
        size_t *path_len = kmalloc(sizeof(int));
		result = copyoutstr((const char *) args[i], arg_addr, (size_t) size[i], path_len);
	    if (result) {
		    kfree(path_len);
		    return result;
	    }

	    kfree(path_len);
		args_copied++;
	}

	*args_copied = NULL;
	*argsv_addr = (userptr_t) (*stackptr - argc*sizeof(int) - sizeof(NULL));
	arg_addr -= (int) arg_addr % sizeof(void *);
	*stackptr = (vaddr_t) arg_addr;

    return 0;
}

/* Switches to new addrspace */
static void switch_as(struct addrspace *as_new) {
	proc_setas(as_new);
	as_activate();
}

int sys_execv(const char *program, char **args) {
    int result;

    if (program == NULL || args == NULL) {
		return EFAULT;
	}

    /* Acquire progname */
    char *progname = kmalloc(PATH_MAX*sizeof(char));
    size_t *path_len = kmalloc(sizeof(int));
    result = copyinstr((const_userptr_t) program, progname, (PATH_MAX + 1), path_len);
    if (result) {
		kfree(progname);
		kfree(path_len);
		return result;
	}

    /* Acquire argc */
    int argc;
    int argnum = 0;
    char *next_arg;
    do {
		result = copyin((const_userptr_t) &args[argnum], (void *) &next_arg, (size_t) sizeof(char *));
		if (result) {
			return result;
		}

	} while (next_arg != NULL && argnum < ARG_MAX);

    if (next_arg != NULL) {
		return E2BIG;
	}

    argc = argnum;

    /* Copy args from old addrspace */
    char **args_in = kmalloc(argc*sizeof(char *));
    int *size = kmalloc(argc*sizeof(int));
    result = copy_from_old(argc, args, args_in, size);
    if (result) {
		kfree(progname);
	    kfree(size);
	    kfree(args_in);
		return result;
	}

    struct vnode *v;
	vaddr_t entrypoint, stackptr;

    /* Open the file */
    result = vfs_open(progname, O_RDONLY, 0, &v);
	if (result) {
		kfree(progname);
		for (int i = 0; i < argc; i++) { /* Free all copied args */
		    kfree(args_in[i]);
	    }
	    kfree(size);
	    kfree(args_in);
		return result;
	}

    /* Fetch the address space of (the current) process */
    struct addrspace *as_old = proc_getas();

    /* Get a new address space */
    struct addrspace *as_new = as_create();
	if (as_new == NULL) { /* New address space is NULL */
		vfs_close(v);
		kfree(progname);
		for (int i = 0; i < argc; i++) {
		    kfree(args_in[i]);
	    }
	    kfree(size);
	    kfree(args_in);
		return ENOMEM;
	}

    /* Switch to as_new and activate it. */
    switch_as(as_new);

    /* Load the executable. */
	result = load_elf(v, &entrypoint);
	if (result) {
        switch_as(as_old); /* Switch bad to old addrspace if error occurs */
		as_destroy(as_new);
		vfs_close(v);
        kfree(progname);
        for (int i = 0; i < argc; i++) {
		    kfree(args_in[i]);
	    }
	    kfree(size);
	    kfree(args_in);
		return result;
	}

    /* Done with the file now. */
	vfs_close(v);

    /* Define the user stack in the address space */
	result = as_define_stack(as_new, &stackptr);
	if (result) {
        switch_as(as_old);
		as_destroy(as_new);
		vfs_close(v);
        kfree(progname);
        for (int i = 0; i < argc; i++) {
		    kfree(args_in[i]);
	    }
	    kfree(size);
	    kfree(args_in);
		return result;
	}

    /* Copy args to new address space */
    userptr_t argsv_addr;
	result = copy_to_new(argc, args_in, size, &stackptr, &argsv_addr);
    if (result) {
        switch_as(as_old);
		as_destroy(as_new);
        kfree(progname);
        for (int i = 0; i < argc; i++) {
		    kfree(args_in[i]);
	    }
	    kfree(size);
	    kfree(args_in);
		return result;
	}

    /* Destroy as_old */
    as_destroy(as_old);

    kfree(progname);
    for (int i = 0; i < argc; i++) {
		    kfree(args_in[i]);
	    }
	kfree(size);
	kfree(args_in);

    /* Warp to user mode. */
	enter_new_process(argc /*argc*/,  argsv_addr /*userspace addr of argv*/,
			  NULL /*userspace addr of environment*/,
			  stackptr, entrypoint);

	/* enter_new_process does not return. */
	panic("enter_new_process returned\n");
	return EINVAL;
}

int sys_waitpid(pid_t curpid, int *status, int options, pid_t *retval) {

    int result = 0;

    lock_acquire(proctable->lock);
    lock_acquire(curproc->lock);

    if (options != 0) {
        lock_release(proctable->lock);
        lock_release(curproc->lock);
        return EINVAL;
    }

    if (!proctable->proc[curpid]) {
        lock_release(proctable->lock);
        lock_release(curproc->lock);
        return ESRCH;
    }

    if (proctable->proc[curpid]->parent_pid != curproc->pid) {
        lock_release(proctable->lock);
        lock_release(curproc->lock);
        return ECHILD;
    }

    if (curpid == curproc->pid) {
        lock_release(proctable->lock);
        lock_release(curproc->lock);
        return ECHILD;
    }

    if (!proctable->proc[curpid]->exited) {
        cv_wait(proctable->proc[curpid]->exit_signal, proctable->lock);
    }

    /* copyout status code */
    if (status != NULL) {
        result = copyout(&proctable->proc[curpid]->exitcode, (userptr_t)status,
                        sizeof(proctable->proc[curpid]->exitcode));
        if (result) {
            lock_release(proctable->lock);
            lock_release(curproc->lock);
            return result;
        }
    }

    /* free here if it is the first proc */
    if (proctable->proc[curpid] != NULL && curpid == 2) {
        proctable->proc[curpid] = NULL;
        proctable->proc[curpid]->exited = 1;
        proc_destroy(proctable->proc[curpid]);
    }

    lock_release(proctable->lock);
    lock_release(curproc->lock);

    *retval = curpid;
    return result;

}

int sys_getpid(pid_t *retval) {
    *retval = curproc->pid;
    return 0;
}

void sys__exit(int exitcode) {

    lock_acquire(proctable->lock);
    lock_acquire(curproc->lock);
    struct proc *exit_proc = curproc;
    pid_t exit_pid = exit_proc->pid;

    exit_proc->exitcode = _MKWAIT_EXIT(exitcode);
    exit_proc->exited = 1;
    cv_broadcast(exit_proc->exit_signal, proctable->lock);
    if (exit_proc->parent_pid >= 0) {
        if (proctable->proc[exit_proc->parent_pid]) {
            if (!proctable->proc[exit_proc->parent_pid]->exited) {
                cv_wait(proctable->proc[exit_proc->parent_pid]->exit_signal,
                        proctable->lock);
            }
        }
    }

    proctable->proc[exit_pid] = NULL;

    lock_release(proctable->lock);
    lock_release(curproc->lock);

    thread_exit();
}
