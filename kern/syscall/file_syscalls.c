#include <types.h>
#include <lib.h>
#include <array.h>
#include <copyinout.h>
#include <uio.h>
#include <fs.h>
#include <syscall.h>
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

int sys_open(const char *filename, int flags, int32_t *retval) {

    int result;
    int fd = 2;
    int found_fd = 0;
    size_t *path_len;
    char *filename_kernel;
    struct vnode *file_vn;

    lock_acquire(curproc->filetable->lock);

    if (filename == NULL) {
        lock_release(curproc->filetable->lock);
        return EFAULT;
    }

    while (!found_fd) {
        fd++;
        if (!curproc->filetable->file[fd]) {
            found_fd = 1;
        }
        else if (fd == OPEN_MAX) {
            lock_release(curproc->filetable->lock);
            return EMFILE;
        }
    }

    path_len = kmalloc(sizeof(size_t));
    filename_kernel = kmalloc(PATH_MAX);
    result = copyinstr((const_userptr_t)filename, filename_kernel, PATH_MAX, path_len);

    if (result) {
        kfree(path_len);
        kfree(filename_kernel);
        lock_release(curproc->filetable->lock);
        return result;
    }

    file_vn = kmalloc(sizeof(struct vnode));
    result = vfs_open(filename_kernel, flags, 0, &file_vn);
    kfree(filename_kernel);
    if (result) {
        lock_release(curproc->filetable->lock);
        return result;
    }

    curproc->filetable->file[fd] = kmalloc(sizeof(struct file));
    curproc->filetable->file[fd]->vn = file_vn;
    curproc->filetable->file[fd]->flags = flags;
    curproc->filetable->file[fd]->offset = 0;
    curproc->filetable->file[fd]->refcount = 1;
    curproc->filetable->file[fd]->lock = lock_create("");

    lock_release(curproc->filetable->lock);

    *retval = fd;
    return 0;
}

int sys_read(int fd, void *buf, size_t buflen, ssize_t *retval) {

    int result;
    ssize_t read_bytes;
    struct iovec *iovec;
    struct uio *uio;

    lock_acquire(curproc->filetable->lock);

    if(fd >= OPEN_MAX || fd < 0) {
        lock_release(curproc->filetable->lock);
        return EBADF;
    }

    if(curproc->filetable->file[fd] == NULL) {
        lock_release(curproc->filetable->lock);
        return EBADF;
    }

    if(curproc->filetable == NULL) {
        lock_release(curproc->filetable->lock);
        return EBADF;
    }

    lock_acquire(curproc->filetable->file[fd]->lock);
    lock_release(curproc->filetable->lock);

    if(curproc->filetable->file[fd]->flags & O_WRONLY) { /*only open for write*/
        lock_release(curproc->filetable->file[fd]->lock);
        return EBADF;
    }

    iovec = kmalloc(sizeof(struct iovec));
    uio = kmalloc(sizeof(struct uio));

    iovec->iov_ubase = buf;
    iovec->iov_len = buflen;
    uio->uio_iov = iovec;
    uio->uio_iovcnt = 1;
    uio->uio_offset = curproc->filetable->file[fd]->offset;
    uio->uio_resid = buflen;
    uio->uio_segflg = UIO_USERSPACE;
    uio->uio_rw = UIO_READ;
    uio->uio_space = curproc->p_addrspace;

    /*VOP_READ deals with the EIO error*/
    result = VOP_READ(curproc->filetable->file[fd]->vn, uio);
    if (result) {
        lock_release(curproc->filetable->file[fd]->lock);
        kfree(uio);
        kfree(iovec);
        return result;
    }

    /*max read bytes - remaining bytes*/
    read_bytes = buflen - uio->uio_resid;
    curproc->filetable->file[fd]->offset += read_bytes;

    lock_release(curproc->filetable->file[fd]->lock);

    *retval = read_bytes;

    kfree(uio);
    kfree(iovec);
    return 0;
}

int sys_write(int fd, const void *buf, size_t nbytes, ssize_t *retval) {

    ssize_t written_bytes;
    struct iovec *iovec;
    struct uio *uio;
    int result;

    lock_acquire(curproc->filetable->lock);

    if (fd >= OPEN_MAX || fd < 0) {
        lock_release(curproc->filetable->lock);
        return EBADF;
    }

    if(curproc->filetable == NULL) {
        lock_release(curproc->filetable->lock);
        return EBADF;
    }

    if(curproc->filetable->file[fd] == NULL) {
        lock_release(curproc->filetable->lock);
        return EBADF;
    }

    lock_acquire(curproc->filetable->file[fd]->lock);
    lock_release(curproc->filetable->lock);

    if (!(curproc->filetable->file[fd]->flags & (O_WRONLY | O_RDWR))) {
        lock_release(curproc->filetable->file[fd]->lock);
        return EBADF;
    }

    iovec = kmalloc(sizeof(struct iovec));
    uio = kmalloc(sizeof(struct uio));

    iovec->iov_ubase = (userptr_t)buf;
    iovec->iov_len = nbytes;
    uio->uio_iov = iovec;
    uio->uio_iovcnt = 1;
    uio->uio_offset = curproc->filetable->file[fd]->offset;
    uio->uio_resid = nbytes;
    uio->uio_segflg = UIO_USERSPACE;
    uio->uio_rw = UIO_WRITE;
    uio->uio_space = curproc->p_addrspace;

    result = VOP_WRITE(curproc->filetable->file[fd]->vn, uio);
    if (result) {
        lock_release(curproc->filetable->file[fd]->lock);
        return result;
    }

    written_bytes = nbytes - uio->uio_resid;
    curproc->filetable->file[fd]->offset += written_bytes;

    kfree(uio);
    kfree(iovec);
    lock_release(curproc->filetable->file[fd]->lock);
    *retval = written_bytes;

    return 0;
}

int sys_lseek(int fd, off_t pos, int whence, int32_t *retval, int32_t *retval2) {
    struct stat *stats;
    off_t endoffile;
    off_t seek_pos;

    if(whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END) {
        return EINVAL;
    }

    lock_acquire(curproc->filetable->lock);

    if(fd >= OPEN_MAX || fd < 0) {
        lock_release(curproc->filetable->lock);
        return EBADF;
    }

    if(curproc->filetable == NULL) {
        lock_release(curproc->filetable->lock);
        return EBADF;
    }

    if(curproc->filetable->file[fd] == NULL) {
        lock_release(curproc->filetable->lock);
        return EBADF;
    }

    lock_acquire(curproc->filetable->file[fd]->lock);
    lock_release(curproc->filetable->lock);

    if (!VOP_ISSEEKABLE(curproc->filetable->file[fd]->vn)) {
        lock_release(curproc->filetable->file[fd]->lock);
        return ESPIPE;
    }

    stats = kmalloc(sizeof(struct stat));
    VOP_STAT(curproc->filetable->file[fd]->vn, stats);
    endoffile = stats->st_size;
    seek_pos = curproc->filetable->file[fd]->offset;

    if(whence == SEEK_SET) {
        seek_pos = pos;
    }
    else if(whence == SEEK_CUR) {
        seek_pos += pos;
    }
    else if(whence == SEEK_END) {
        seek_pos = endoffile + pos;
    }

    if(seek_pos < 0) {
        lock_release(curproc->filetable->file[fd]->lock);
        return EINVAL; /*negative seek position*/
    }

    curproc->filetable->file[fd]->offset = seek_pos; /*update seek position*/

    lock_release(curproc->filetable->file[fd]->lock);
    /*because seek_pos is 64 bits we need to split it to return it (bswap.c)*/
    *retval = seek_pos >> 32;
    *retval2 = seek_pos & 0xffffffff;
    return 0;
}

int sys_close(int fd, int32_t *retval) {

    lock_acquire(curproc->filetable->lock);
    if (fd < 0 || fd >= OPEN_MAX) {
        lock_release(curproc->filetable->lock);
        return EBADF;
    }

    if(curproc->filetable->file[fd] == NULL) {
        lock_release(curproc->filetable->lock);
        return EBADF;
    }

    curproc->filetable->file[fd]->refcount--;

    /* Destroy file if there are no more references to it, free up file handle*/
    if (curproc->filetable->file[fd]->refcount == 0) {
        vfs_close(curproc->filetable->file[fd]->vn);
        lock_destroy(curproc->filetable->file[fd]->lock);
        kfree(curproc->filetable->file[fd]);
    }
    curproc->filetable->file[fd] = NULL;

    lock_release(curproc->filetable->lock);
    *retval = 0;
    return 0;
}

int sys_dup2(int oldfd, int newfd, int32_t *retval) {

    int result;

    lock_acquire(curproc->filetable->lock);

    if (oldfd < 0 || oldfd >= OPEN_MAX || newfd < 0 || newfd >= OPEN_MAX) {
        lock_release(curproc->filetable->lock);
        return EBADF;
    }

    if(curproc->filetable->file[oldfd] == NULL) {
        lock_release(curproc->filetable->lock);
        return EBADF;
    }

    if (oldfd == newfd) {
        lock_release(curproc->filetable->lock);
        *retval = oldfd;
        return 0;
    }

    /* if new file handle is open, close it */
    if (curproc->filetable->file[newfd]) {
        lock_release(curproc->filetable->lock);
        result = sys_close(newfd, NULL);
        if (result) {
            return result;
        }
    }

    /* both pointers point to the same object */
    lock_acquire(curproc->filetable->file[oldfd]->lock);
    curproc->filetable->file[oldfd]->refcount++;
    curproc->filetable->file[newfd] = curproc->filetable->file[oldfd];
    lock_release(curproc->filetable->file[oldfd]->lock);

    lock_release(curproc->filetable->lock);
    *retval = newfd;
    return 0;
}

int sys_chdir(const char *pathname, int32_t *retval) {

    int result;
    char *pathname_kernel;
    size_t *strlen;

    lock_acquire(curproc->filetable->lock);

    if (pathname == NULL) {
        lock_release(curproc->filetable->lock);
        return EFAULT;
    }

    strlen = kmalloc(sizeof(size_t));
    pathname_kernel = kmalloc(PATH_MAX);
    result = copyinstr((const_userptr_t)pathname, pathname_kernel, PATH_MAX, strlen);

    if (result) {
        kfree(strlen);
        kfree(pathname_kernel);
        lock_release(curproc->filetable->lock);
        return result;
    }

    result = vfs_chdir(pathname_kernel);
    kfree(pathname_kernel);
    if (result) {
        lock_release(curproc->filetable->lock);
        return result;
    }

    lock_release(curproc->filetable->lock);
    *retval = 0;
    return 0;
}

int sys___getcwd(char *buf, size_t buflen, int32_t *retval) {

    struct iovec *iovec;
    struct uio *uio;
    int result;
    int32_t data_len;

    lock_acquire(curproc->filetable->lock);

    if (buf == NULL) {
        lock_release(curproc->filetable->lock);
        return EFAULT;
    }

    iovec = kmalloc(sizeof(struct iovec));
    uio = kmalloc(sizeof(struct uio));

    iovec->iov_ubase = (userptr_t)buf;
    iovec->iov_len = buflen;
    uio->uio_iov = iovec;
    uio->uio_iovcnt = 1;
    uio->uio_offset = 0;
    uio->uio_resid = buflen;
    uio->uio_segflg = UIO_USERSPACE;
    uio->uio_rw = UIO_READ;
    uio->uio_space = curproc->p_addrspace;

    result = vfs_getcwd(uio);
    if (result) {
        lock_release(curproc->filetable->lock);
        return result;
    }

    data_len = buflen - uio->uio_resid; /*return data stored in directory*/
    *retval = data_len;

    lock_release(curproc->filetable->lock);
    kfree(uio);
    kfree(iovec);

    return 0;
}
