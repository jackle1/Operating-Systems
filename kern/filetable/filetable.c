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

int filetable_init (struct proc *proc) {

    int result;
    struct vnode *stdin_vn;
    struct vnode *stdout_vn;
    struct vnode *stderr_vn;
    char path1[] = "con:";
    char path2[] = "con:";
    char path3[] = "con:";

    proc->filetable = kmalloc(sizeof(struct filetable));
    proc->filetable->lock = lock_create("");

    stdin_vn = kmalloc(sizeof(struct vnode));
    result = vfs_open(path1, O_RDONLY, 0, &stdin_vn);
    if (result) {
        return result;
    }

    proc->filetable->file[0] = kmalloc(sizeof(struct file));
    proc->filetable->file[0]->vn = stdin_vn;
    proc->filetable->file[0]->flags = O_RDONLY;
    proc->filetable->file[0]->offset = 0;
    proc->filetable->file[0]->refcount = 1;
    proc->filetable->file[0]->lock = lock_create("");

    stdout_vn = kmalloc(sizeof(struct vnode));
    result = vfs_open(path2, O_WRONLY, 0, &stdout_vn);
    if (result) {
        return result;
    }

    proc->filetable->file[1] = kmalloc(sizeof(struct file));
    proc->filetable->file[1]->vn = stdout_vn;
    proc->filetable->file[1]->flags = O_WRONLY;
    proc->filetable->file[1]->offset = 0;
    proc->filetable->file[1]->refcount = 1;
    proc->filetable->file[1]->lock = lock_create("");

    stderr_vn = kmalloc(sizeof(struct vnode));
    result = vfs_open(path3, O_WRONLY, 0, &stderr_vn);
    if (result) {
        return result;
    }

    proc->filetable->file[2] = kmalloc(sizeof(struct file));
    proc->filetable->file[2]->vn = stderr_vn;
    proc->filetable->file[2]->flags = O_WRONLY;
    proc->filetable->file[2]->offset = 0;
    proc->filetable->file[2]->refcount = 1;
    proc->filetable->file[2]->lock = lock_create("");

    /* rest of files uninitialized/unopened */
    for (int i = 3; i < OPEN_MAX; i++) {
        proc->filetable->file[i] = NULL;
    }

    return 0;
}

void filetable_destroy (struct filetable *filetable) {
    for (int i = 0; i < OPEN_MAX; i++) {
        if (filetable->file[i]) {
            file_destroy(filetable->file[i]);
        }
    }
    kfree(filetable);
}

void file_destroy (struct file *file) {
    vfs_close(file->vn);
    file->refcount--;
    if (file->refcount == 0) {
        lock_destroy(file->lock);
        kfree(file);
    }
    file = NULL;
}

int filetable_copy (struct proc *oldproc, struct proc *newproc) {
    lock_acquire(oldproc->filetable->lock);
    newproc->filetable = kmalloc(sizeof(struct filetable));
    if (!newproc->filetable) {
        lock_release(oldproc->filetable->lock);
        return ENOMEM;
    }
    newproc->filetable->lock = lock_create("");
    for (int i = 0; i < OPEN_MAX; i++) {
        /* both filetables point to same files */
        newproc->filetable->file[i] = oldproc->filetable->file[i];
        if (oldproc->filetable->file[i]) {
            oldproc->filetable->file[i]->refcount++;
        }
    }
    lock_release(oldproc->filetable->lock);
    return 0;
}
