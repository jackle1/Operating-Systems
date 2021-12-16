#ifndef _FILETABLE_H_
#define _FILETABLE_H_

#include <types.h>
#include <lib.h>
#include <array.h>
#include <copyinout.h>
#include <uio.h>
#include <fs.h>
#include <current.h>
#include <vfs.h>
#include <filetable.h>
#include <synch.h>
#include <vnode.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <limits.h>
#include <proc.h>
#include <kern/seek.h>
#include <stat.h>

struct filetable {
    struct file *file[OPEN_MAX];    /* array of file pointers, one per fd */
    struct lock *lock;              /* lock for this structure */
};

struct file {
    struct vnode *vn;               /* corresponding vnode */
    int flags;                      /* relevant file flags */
    off_t offset;                   /* file offset */
    int refcount;                   /* Reference count */
    struct lock *lock;              /* lock for this structure */
};

/*
 *    Filetable operations to initialize and destroy filetable/file
 *
 *    filetable_init        - Initialize filetable with first three file
 *                            descripters 0, 1, 2 pointing to STDIN, STDOUT
 *                            and STDERR respectively, rest of file are NULL
 *    filetable_destroy     - Cleanup filetable initialized with filetable_init
 *    file_destroy          - Cleanup a file object
 */
int filetable_init (struct proc *proc);
void filetable_destroy (struct filetable *filetable);
void file_destroy (struct file *file);
int filetable_copy (struct proc *oldproc, struct proc *newproc);

#endif /* _FILETABLE_H_ */
