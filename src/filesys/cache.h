#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include <hash.h>
#include "devices/block.h"
#include "threads/synch.h"

#define BLOCK_SECTOR_SIZE 512
#define MAX_CACHE_SIZE 64

struct cache_entry {
  block_sector_t sector_num;
  struct list_elem l_elem;
  struct hash_elem h_elem;
  struct lock lock;
  bool dirty;
  char data[BLOCK_SECTOR_SIZE];
};

void cache_init (void);
struct cache_entry *cache_find (block_sector_t sector_num);
struct cache_entry *cache_add (block_sector_t sector_num);
void cache_read (struct cache_entry *ce, void *dest);
void cache_write (struct cache_entry *ce, void *dest);
void cache_flush (void);
void cache_destroy (void);

#endif
