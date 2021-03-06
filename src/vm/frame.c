#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "vm/frame.h"

#define FREE_PAGES_START_OFFSET 1024 * 1024

static struct frame_entry *frame_table;
static struct lock frame_table_lock;  // only used in clock algorithm
static int num_user_pages;            // only used in clock algorithm
static int num_kernel_pages;          // used in getting frame_entry

void
frame_init (size_t user_page_limit)
{
  uint8_t *free_start = ptov (FREE_PAGES_START_OFFSET);
  uint8_t *free_end = ptov (init_ram_pages * PGSIZE);
  size_t free_pages = (free_end - free_start) / PGSIZE;
  num_user_pages = free_pages / 2;
  
  if ((size_t) num_user_pages > user_page_limit)
    num_user_pages = user_page_limit;
  num_kernel_pages = free_pages - num_user_pages;

  frame_table = (struct frame_entry *) malloc (sizeof(struct frame_entry) 
                                               * num_user_pages);
  ASSERT (frame_table != NULL);
  lock_init (&frame_table_lock);

  int i;
  for (i = 0; i < (int) num_user_pages; i++) {
    struct frame_entry *entry = &frame_table[i]; // used on frame entry access
    lock_init (&entry->lock);
  }
}

static struct frame_entry *
kpage_to_frame_entry (void *kpage)
{
  int index = (unsigned) (vtop (kpage) - num_kernel_pages * PGSIZE - 
                          FREE_PAGES_START_OFFSET) / PGSIZE;
  return &frame_table[index];
}

static void * 
frame_entry_to_kpage (struct frame_entry *entry)
{
  int index = ((unsigned) entry - (unsigned) frame_table) / 
               sizeof (struct frame_entry);
  return ptov (FREE_PAGES_START_OFFSET) + (num_kernel_pages + index) * PGSIZE;
}

/* chooses frame, updates sup_page_table entries, writes data if needed */
/* Entering this function you already have the frame table lock and the lock for
 the entry you own*/
static void * 
evict (struct sup_page_entry *page_entry, bool pinned)
{
  struct thread *t = thread_current ();
  struct frame_entry *evict_entry = NULL;

  while (true) {

    int i;
    for (i = 1; i < num_user_pages; i++) {  // frame entries start at 1
      struct frame_entry *entry = &frame_table[i];
      if (lock_try_acquire (&entry->lock)) {

        if (entry->thread == NULL) {  // found an empty frame
          void *kpage = palloc_get_page (PAL_USER);
          kpage = pg_round_down (kpage);
          pagedir_set_page (t->pagedir, page_entry->upage, kpage, 
                            page_entry->writable);
          entry->thread = t;
          entry->upage = page_entry->upage;
          entry->pinned = pinned;
          lock_release (&frame_table_lock);
          lock_release (&entry->lock);
          return kpage;
        }

        if (pagedir_is_accessed (entry->thread->pagedir, entry->upage)) {
          pagedir_set_accessed (entry->thread->pagedir, entry->upage, false);

        } else if (!entry->pinned) { // frame neither accessed nor pinned

          if (!lock_try_acquire (&entry->thread->exit_lock)) { // target exit
            lock_release (&entry->lock);
            continue;
          }
          evict_entry = entry;
          lock_release (&frame_table_lock);
          break;
        } 

        lock_release (&entry->lock);
      }
    }

    if (evict_entry != NULL) { 

      page_evict (evict_entry->thread, evict_entry->upage);  
      lock_release (&evict_entry->thread->exit_lock);

      evict_entry->thread = t;
      evict_entry->upage = page_entry->upage;
      evict_entry->pinned = pinned;

      lock_release (&evict_entry->lock);
      return frame_entry_to_kpage (evict_entry);
    }

  }
}

/* allocates page in physical memory for a specific thread's virtual memory 
   deals with eviction if necessary.
*/
void *
frame_add (struct sup_page_entry *page_entry, bool pinned) 
{
  struct thread *t = thread_current ();
  lock_acquire (&frame_table_lock);

  void *kpage = page_entry->zeroed ? palloc_get_page (PAL_USER | PAL_ZERO)
                                   : palloc_get_page (PAL_USER); 
  if (kpage == NULL) {

    kpage = evict (page_entry, pinned);
    if (pagedir_get_page (t->pagedir, page_entry->upage) == NULL)
      pagedir_set_page (t->pagedir, page_entry->upage, kpage, 
                        page_entry->writable);

  } else {

    pagedir_set_page (t->pagedir, page_entry->upage, kpage, 
                      page_entry->writable);

    kpage = pg_round_down (kpage);
    struct frame_entry *frame_entry = kpage_to_frame_entry (kpage);
    lock_acquire (&frame_entry->lock);
    lock_release (&frame_table_lock);

    frame_entry->thread = thread_current ();
    frame_entry->upage = page_entry->upage;
    frame_entry->pinned = pinned;

    lock_release (&frame_entry->lock);

  }

  return kpage;
}

void
frame_remove (void *kpage)
{
  struct frame_entry *entry = kpage_to_frame_entry (kpage);

  lock_acquire (&entry->lock);
  
  palloc_free_page (kpage);
  entry->upage = entry->thread = NULL;
  entry->pinned = false;

  lock_release (&entry->lock);
}

/*  This function will pin or unpin upage to the frame table. This 
    memory cannot be accessed by another thread until it is unpinned. 
*/
static bool
set_pin_status (const void *upage, bool pinned)
{
  struct thread *t = thread_current();
  void *kpage = pagedir_get_page (t->pagedir, upage);
  if (kpage == NULL) 
    return false;
  
  kpage = pg_round_down (kpage);
  struct frame_entry *entry = kpage_to_frame_entry (kpage);
  lock_acquire (&entry->lock);

  if (entry->thread != thread_current ()) {
    lock_release (&entry->lock);
    return false;
  }

  entry->pinned = pinned; 
  lock_release (&entry->lock);
  return true;
}

bool
frame_pin (const void *upage)
{
  return set_pin_status (upage, true);
}

bool
frame_unpin (const void *upage) 
{
  return set_pin_status (upage, false);
}
