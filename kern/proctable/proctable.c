#include <types.h>
#include <spl.h>
#include <proc.h>
#include <current.h>
#include <addrspace.h>
#include <vnode.h>
#include <limits.h>
#include <proctable.h>


/*
 * proctable
 */
struct proctable *proctable;

void
proctable_bootstrap(void)
{
    proctable = kmalloc(sizeof(struct proctable));
    proctable->pid = PID_MIN;
    for (int i = 0; i < PID_MAX; i++) {
        proctable->proc[i] = NULL;
    }
    proctable->lock = lock_create("");
}

void
proctable_destroy(void)
{
    for (int i = 0; i < PID_MAX; i++) {
        if (proctable->proc[i]) {
            proc_destroy(proctable->proc[i]);
            proctable->proc[i] = NULL;
        }
    }
    lock_destroy(proctable->lock);
    kfree(proctable);
}
