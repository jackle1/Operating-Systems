#ifndef _PROCTABLE_H_
#define _PROCTABLE_H_

#include <types.h>
#include <lib.h>
#include <array.h>
#include <synch.h>
#include <limits.h>
#include <proc.h>

struct proctable {
    struct proc *proc[PID_MAX];   /* keeps track of all processes */
    struct lock *lock;            /* lock for this structure */
    pid_t pid;
};

extern struct proctable *proctable;

/*
 * Create the process table structure.
 */
void proctable_bootstrap(void);

/*
 * Destroy process table structure
 */
void proctable_destroy(void);

#endif /* _PROCTABLE_H_ */
