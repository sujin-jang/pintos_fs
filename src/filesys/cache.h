#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include "devices/disk.h"
#include <list.h>
#include <hash.h>

struct cache_entry			  /* Buffer Cache Entry */
{	
	disk_sector_t sec_no;
  	uint8_t block[DISK_SECTOR_SIZE];
  	bool dirty;
  	bool access;

	struct list_elem elem;
};

void cache_init (void);
struct cache_entry * cache_find (disk_sector_t sec_no);

bool cache_write (disk_sector_t sec_no, void* buffer, int ofs, int size);
bool cache_read (disk_sector_t sec_no, void* buffer, int ofs, int size);
#endif /* filesys/cache.h */
