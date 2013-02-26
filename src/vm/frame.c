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
static int max_frame_table_index;
static int num_kernel_pages;

void
frame_init (size_t user_page_limit)
{
  uint8_t *free_start = ptov (FREE_PAGES_START_OFFSET);
  uint8_t *free_end = ptov (init_ram_pages * PGSIZE);
  size_t free_pages = (free_end - free_start) / PGSIZE;
  size_t user_pages = free_pages / 2;
  
  if (user_pages > user_page_limit)
    user_pages = user_page_limit;
  num_kernel_pages = free_pages - user_pages;

  frame_table = (struct frame_entry *) malloc (sizeof(struct frame_entry) 
                                               * user_pages);
  lock_init (&frame_table_lock);

  /* we use the entry->lock every time we edit the entry */
  int i;
  for (i = 0; i < (int) user_pages; i++) {
    struct frame_entry *entry = &frame_table[i];
    lock_init (&entry->lock);
  }
  max_frame_table_index = user_pages - 1;
}

static struct frame_entry *
kpage_to_frame_entry (void *kpage)
{
  void *phys_addr = (void *) vtop (kpage);
  int index = (unsigned) (phys_addr - FREE_PAGES_START_OFFSET - 
                          num_kernel_pages * PGSIZE) / PGSIZE;
  ASSERT (index <= max_frame_table_index);
  return &frame_table[index];
}

/* allocates page in physical memory for a specific thread's virtual memory */
void *
frame_add (struct thread *thread, const void *upage, bool zero_page, bool pinned) 
{
  void *kpage = zero_page ? palloc_get_page (PAL_USER | PAL_ZERO) 
                          : palloc_get_page (PAL_USER); 
  if (kpage == NULL) {
    //Will implemenent swping to swp disk here
    ASSERT (false);
  }

  struct frame_entry *entry = kpage_to_frame_entry (kpage);
  lock_acquire (&entry->lock);

  entry->thread = thread;
  entry->upage = upage;
  entry->pinned = pinned;

  lock_release (&entry->lock);
  return kpage;
}

/*EVICT : Should check if pinned before removing */
void
frame_remove (void *kpage)
{
  struct frame_entry *entry = kpage_to_frame_entry (kpage);
  lock_acquire (&entry->lock);
 // palloc_free_page (kpage);
  entry->upage = entry->thread = NULL;
  entry->pinned = false;

  lock_release (&entry->lock);
}

/* 
    This function will pin or unpin upage to the frame table. This 
    memory cannot be accessed by another thread until it is unpinned. 
    this restriction can be circumvented by a process when it exits.
    
    Pinning is only done in syscalls and setup_stack, not eviction.
 */
static bool
set_pin_status (const void *upage, bool pinned)
{
  struct thread *t = thread_current();
  void *kpage = pagedir_get_page (t->pagedir, upage);
  if (kpage == NULL) 
    return false;
  
  struct frame_entry *entry = kpage_to_frame_entry (kpage);
  lock_acquire (&entry->lock);

  if (entry->thread != thread_current()) {
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
frame_unpin (void *upage) 
{
  return set_pin_status (upage, false);
}
