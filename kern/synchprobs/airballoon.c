/*
 * Driver code for airballoon problem
 */
#include <types.h>
#include <lib.h>
#include <thread.h>
#include <test.h>
#include <synch.h>

#define N_LORD_FLOWERKILLER 8
#define NROPES 16
static int ropes_left = NROPES;
static int threads_running = N_LORD_FLOWERKILLER + 4; //main, balloon, dandelion, marigold, N-flowerkillers
static int threads_asleep = 0; //no threads sleeping initially

//MY DATA STRUCTURES:
struct rope {
	struct lock *rope_lk;
	volatile bool rope_severed;
};

struct hook {
    struct lock *hook_lock;
	struct rope *ropes;
    volatile int hook_index;
};

struct stake {
	struct lock *stake_lock;
	struct rope *ropes;
	volatile int stake_index;
};

static struct hook *hooks;
static struct stake *stakes;
static struct rope *ropes;


//MY LOCKS:
static struct lock *ropes_left_lk; //held when decrementing ropes_left after a rope is severed
static struct lock *thread_lk; 
static struct cv *thread_cv;

/*
 * Describe your design and any invariants or locking protocols
 * that must be maintained. Explain the exit conditions. How
 * do all threads know when they are done?
 */

static
void
dandelion(void *p, unsigned long arg)
{
	int hook_ind;

	(void)p;
	(void)arg;

	kprintf("Dandelion thread starting\n");

	while(ropes_left > 0) { //still ropes left
		hook_ind = random() % NROPES; //the randomly choose a hook
		lock_acquire(hooks[hook_ind].hook_lock); //lock the hook (technically unnecessary though since only Dandelion is touching the hooks)

		lock_acquire(hooks[hook_ind].ropes[hook_ind].rope_lk); //acquire rope lock
		if (hooks[hook_ind].ropes[hook_ind].rope_severed == false) {  //if the rope corresponding to the selected hook has not been severed yet

			//sever the rope
			hooks[hook_ind].ropes[hook_ind].rope_severed = true;

			kprintf("Dandelion severed rope %d\n", hook_ind);

			lock_acquire(ropes_left_lk); //acquire rope lock so that we can decrement the rope counter
			ropes_left--;
			lock_release(ropes_left_lk);

			thread_yield(); //yield thread after severing a rope
		}

		lock_release(hooks[hook_ind].ropes[hook_ind].rope_lk);
		lock_release(hooks[hook_ind].hook_lock);
	}
	
	//all ropes have been severed
	lock_acquire(thread_lk);
	threads_asleep++; //increment asleep counter so that the main thread knows when to stop yielding
	cv_wait(thread_cv, thread_lk); //release the lock and sleep until awoken by main thread to print done
	kprintf("Dandelion thread done\n");
	threads_running--; //decrement running thread counter so that main thread knows when it can cleanup print done
	lock_release(thread_lk);

	thread_exit();

}

static
void
marigold(void *p, unsigned long arg)
{
	int stake_ind;

	(void)p;
	(void)arg;

	kprintf("Marigold thread starting\n");

	while(ropes_left > 0) { //still ropes left
		stake_ind = random() % NROPES; //randomly choose a stake
		lock_acquire(stakes[stake_ind].stake_lock); //lock the chosen stake

		lock_acquire(stakes[stake_ind].ropes[stakes[stake_ind].stake_index].rope_lk); //acquire rope lock
		if (stakes[stake_ind].ropes[stakes[stake_ind].stake_index].rope_severed == false) {  //if it has not been severed yet

			//sever the rope
			stakes[stake_ind].ropes[stakes[stake_ind].stake_index].rope_severed = true;

			kprintf("Marigold severed rope %d from stake %d\n", stakes[stake_ind].stake_index, stake_ind);

			lock_acquire(ropes_left_lk); //acquire rope left lock so that we can decrement the rope counter
			ropes_left--;
			lock_release(ropes_left_lk);
			
			thread_yield(); //yield thread after severing a rope
		}

		lock_release(stakes[stake_ind].ropes[stakes[stake_ind].stake_index].rope_lk);
		lock_release(stakes[stake_ind].stake_lock);

	}
	
	//all ropes have been severed
	lock_acquire(thread_lk);
	threads_asleep++;
	cv_wait(thread_cv, thread_lk);
	kprintf("Marigold thread done\n");
	threads_running--;
	lock_release(thread_lk);

	thread_exit();
}

static
void
flowerkiller(void *p, unsigned long arg)
{
	(void)p;
	(void)arg;

	kprintf("Lord FlowerKiller thread starting\n");
	
	while(ropes_left > 1) { //still at least 2 ropes left
		int swap_one_ind = random() % NROPES; //randomly select a first rope to swap
		int swap_two_ind = random() % NROPES; //randomly select a second rope to swap

		while((swap_one_ind == swap_two_ind) || (swap_one_ind > swap_two_ind)) {
			swap_one_ind = random() % NROPES; //make sure that flowerkiller is swapping the same rope
			swap_two_ind = random() % NROPES; //prevents deadlock because flowerkillers won't deadlock when they grab the inverse of the other's ropes 
		}
		
		lock_acquire(stakes[swap_one_ind].stake_lock); //lock first stake
		lock_acquire(stakes[swap_two_ind].stake_lock); //lock second stake

		lock_acquire(stakes[swap_one_ind].ropes[stakes[swap_one_ind].stake_index].rope_lk); //lock rope associated with first stake
		lock_acquire(stakes[swap_two_ind].ropes[stakes[swap_two_ind].stake_index].rope_lk); //lock rope associated with second stake

		if ((stakes[swap_one_ind].ropes[stakes[swap_one_ind].stake_index].rope_severed == false) //if both ropes are still attached
			&& (stakes[swap_two_ind].ropes[stakes[swap_two_ind].stake_index].rope_severed == false)) {

			//swap the ropes
			int temp = stakes[swap_one_ind].stake_index;
			stakes[swap_one_ind].stake_index = stakes[swap_two_ind].stake_index;
			stakes[swap_two_ind].stake_index = temp;
			
			kprintf("Lord FlowerKiller switched rope %d from stake %d to stake %d\n", stakes[swap_one_ind].stake_index, swap_two_ind, swap_one_ind);
			kprintf("Lord FlowerKiller switched rope %d from stake %d to stake %d\n", stakes[swap_two_ind].stake_index, swap_one_ind, swap_two_ind);

			thread_yield(); //yield thread after severing a rope
		}

		lock_release(stakes[swap_two_ind].ropes[stakes[swap_two_ind].stake_index].rope_lk);
		lock_release(stakes[swap_one_ind].ropes[stakes[swap_one_ind].stake_index].rope_lk);

		lock_release(stakes[swap_two_ind].stake_lock);
		lock_release(stakes[swap_one_ind].stake_lock);
	} 
	
	//all ropes have been severed
	lock_acquire(thread_lk);
	threads_asleep++;
	cv_wait(thread_cv, thread_lk);
	kprintf("Lord FlowerKiller thread done\n");
	threads_running--;
	lock_release(thread_lk);

	thread_exit();
}

static
void
balloon(void *p, unsigned long arg)
{
	(void)p;
	(void)arg;

	kprintf("Balloon thread starting\n");

	while(1) { //keep looping while there are ropes left
		if(ropes_left == 0) { 
			break;
		}
	}
	
	kprintf("Balloon freed and Prince Dandelion escapes!\n");

	lock_acquire(thread_lk);
	threads_asleep++;
	cv_wait(thread_cv, thread_lk);
	kprintf("Balloon thread done\n");
	threads_running--;
	lock_release(thread_lk);

	thread_exit();
}


int
airballoon(int nargs, char **args)
{

	int err = 0, i;

	(void)nargs;
	(void)args;
	(void)ropes_left;

	//CREATE LOCKS:
	ropes_left_lk = lock_create(""); //lock used to change ropes_left
	thread_lk = lock_create(""); //threads lock
	thread_cv = cv_create(""); //threads condition variable

	//ALLOCATE SPACE FOR ARRAYS:
	ropes = kmalloc(sizeof(struct rope) * NROPES);
	hooks = kmalloc(sizeof(struct hook) * NROPES); 
	stakes = kmalloc(sizeof(struct stake) * NROPES);

	//INITIALIZE ROPES, STAKES, HOOKS, AND THEIR LOCKS:
	for(int j = 0; j < NROPES; j++) {
		ropes[j].rope_lk = lock_create("");
		hooks[j].hook_lock = lock_create("");
		stakes[j].stake_lock = lock_create("");
		hooks[j].hook_index = j;
		stakes[j].stake_index = j;
		ropes[j].rope_severed = false;
		hooks[j].ropes = ropes;
		stakes[j].ropes = ropes;
	}

	err = thread_fork("Marigold Thread",
			  NULL, marigold, NULL, 0);
	if(err)
		goto panic;

	err = thread_fork("Dandelion Thread",
			  NULL, dandelion, NULL, 0);
	if(err)
		goto panic;

	for (i = 0; i < N_LORD_FLOWERKILLER; i++) {
		err = thread_fork("Lord FlowerKiller Thread",
				  NULL, flowerkiller, NULL, 0);
		if(err)
			goto panic;
	}

	err = thread_fork("Air Balloon",
			  NULL, balloon, NULL, 0);
	if(err)
		goto panic;

	goto done;
panic:
	panic("airballoon: thread_fork failed: %s)\n",
	      strerror(err));

done:
	while(threads_asleep != (3 + N_LORD_FLOWERKILLER) ) { //yield until Dandelion, Balloon, Marigold, and the N Flowerkillers are asleep
		thread_yield();
	}

	//Wake up the sleeping threads to allow them to print their finishing statements
	lock_acquire(thread_lk);
	cv_broadcast(thread_cv, thread_lk);
	lock_release(thread_lk);

	while(threads_running > 1) { //wait until all other threads are finished printing their statements and exited
		thread_yield();
	}

	//CLEANUP:
	ropes_left = NROPES; //reset variables in case it is run again without restarting kernel
	threads_asleep = 0;
	threads_running = N_LORD_FLOWERKILLER + 4;

	lock_destroy(ropes_left_lk); //destroy locks
	lock_destroy(thread_lk);
	for(int k=0; k < NROPES; k++) { //destroy the data structure locks 
		lock_destroy(hooks[k].hook_lock);
		lock_destroy(stakes[k].stake_lock);
		lock_destroy(ropes[k].rope_lk);
	}
	cv_destroy(thread_cv); //destroy cv

	kfree(hooks); //free allocated space
	kfree(stakes);
	kfree(ropes);

	kprintf("Main thread done\n");

	return 0;
}
