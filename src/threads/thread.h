#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <hash.h>
#include <list.h>
#include <stdint.h>

#include "synch.h"

/* States in a thread's life cycle. */
enum thread_status
  {
    THREAD_RUNNING,     /* Running thread. */
    THREAD_READY,       /* Not running but ready to run. */
    THREAD_BLOCKED,     /* Waiting for an event to trigger. */
    THREAD_DYING        /* About to be destroyed. */
  };

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0                       /* Lowest priority. */
#define PRI_DEFAULT 31                  /* Default priority. */
#define PRI_MAX 63                      /* Highest priority. */
#define NUM_PRIORITIES 64								/* number of priorities */

#define NOBODY -1

#define MAX_FD_INDEX 129                /* maximum possible fd value */

/* Struct which stores mmapped file information */
struct mmap_entry {
  struct file *file;
  void *addr;
  int length;
};

/* A kernel thread or user process.

   Each thread structure is stored in its own 4 kB page.  The
   thread structure itself sits at the very bottom of the page
   (at offset 0).  The rest of the page is reserved for the
   thread's kernel stack, which grows downward from the top of
   the page (at offset 4 kB).  Here's an illustration:

        4 kB +---------------------------------+
             |          kernel stack           |
             |                |                |
             |                |                |
             |                V                |
             |         grows downward          |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             +---------------------------------+
             |              magic              |
             |                :                |
             |                :                |
             |               name              |
             |              status             |
        0 kB +---------------------------------+

   The upshot of this is twofold:

      1. First, `struct thread' must not be allowed to grow too
         big.  If it does, then there will not be enough room for
         the kernel stack.  Our base `struct thread' is only a
         few bytes in size.  It probably should stay well under 1
         kB.

      2. Second, kernel stacks must not be allowed to grow too
         large.  If a stack overflows, it will corrupt the thread
         state.  Thus, kernel functions should not allocate large
         structures or arrays as non-static local variables.  Use
         dynamic allocation with malloc() or palloc_get_page()
         instead.

   The first symptom of either of these problems will probably be
   an assertion failure in thread_current(), which checks that
   the `magic' member of the running thread's `struct thread' is
   set to THREAD_MAGIC.  Stack overflow will normally change this
   value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
   the run queue (thread.c), or it can be an element in a
   semaphore wait list (synch.c).  It can be used these two ways
   only because they are mutually exclusive: only a thread in the
   ready state is on the run queue, whereas only a thread in the
   blocked state is on a semaphore wait list. */
struct thread
  {
    /* Owned by thread.c. */
    tid_t tid;                          /* Thread identifier. */
    enum thread_status status;          /* Thread state. */
    char name[16];                      /* Name (for debugging purposes). */
    uint8_t *stack;                     /* Saved stack pointer. */
    struct list_elem allelem;           /* List element for all threads list. */
#ifdef USERPROG
    /* Owned by userprog/process.c. */
    uint32_t *pagedir;                  /* Page directory. */
#endif

    /* Owned by thread.c. */
    unsigned magic;                     /* Detects stack overflow. */

		int64_t ticksToWake;								/* stores the time at which to wake */

    int basePriority;                   /* Only used in priority donation */
    int currPriority;                   /* Used in both priority donation and mlfqs */

    struct list_elem readyElem;         /* Used for normal and mlfqs ready_lists */
		struct list_elem timerWaitElem;			/* Used for wait_list in timer functions */
    struct list_elem semaWaitElem;			/* Used in synch.c for sema->waiters */

		struct list locksHeld;		
		struct lock *lockDesired;

		int niceness;												/* Factor used to determine priority */
		int recentCPU;											/* Recent CPU usage stored as FP*/

    /* the following for project 2 */
    struct thread *parent;
    struct list children_exit_info;
    struct list_elem *exit_info_elem;    /* elem stored in parent's list */
    struct semaphore child_exec_sema;
    struct semaphore child_wait_sema;
    bool child_exec_success;
    int pid_waiting_on;
    int exit_status;

    /* entries begin at index 2 to ensure one-to-one mapping between
       fds and indexes */
    struct file *file_ptrs[MAX_FD_INDEX + 1];
    struct file *my_exec;
    int next_open_file_index;

    struct lock wait_lock;               /* used to prevent race conditions in syscall wait */

   /* Project three additions */
    struct lock sup_page_table_lock;
    struct hash *sup_page_table;
    struct mmap_entry mmap_files[MAX_FD_INDEX + 1]; // mmap_ids = fds
    void *exec_addr;                      // look in load_segment
    int exec_length;
    void *esp;
    struct lock exit_lock; // used to synchronize eviction during exit 
  };

struct exit_info
{
  tid_t tid;
  int exit_status;
  struct thread *child;
  struct list_elem elem;
};

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void);
void thread_unblock (struct thread *);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);

/*Our functions written for thread.h */
void thread_sleep (int64_t releaseTicks);
void thread_wake_all_ready (int64_t currTicks);
bool tick_cmp_fn (const struct list_elem *a, const struct list_elem *b, void *aux);
bool thread_readylist_pri_cmp_fn (const struct list_elem *a, 
																	const struct list_elem *b, void *aux UNUSED);
bool thread_semawaiters_pri_cmp_fn (const struct list_elem *a, 
																		const struct list_elem *b, void *aux UNUSED);
void thread_mlfqs_update_load_avg (void);
void thread_mlfqs_update_all_recent_cpu (void);
void thread_mlfqs_update_all_priorities (void);

/* Performs some operation on thread t, given auxiliary data AUX. */
typedef void thread_action_func (struct thread *t, void *aux);
void thread_foreach (thread_action_func *, void *);

int thread_get_priority (void);
void thread_set_priority (int);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

#endif /* threads/thread.h */
