#include "filesys/cache.h"
#include "filesys/filesys.h"
#include "threads/synch.h"
#include "userprog/process.h"

// struct hash buffer_cache;	/* Buffer Cache */

#define CACHE_SIZE_LIMIT 64

struct list buffer_cache; /* Buffer Cache */
struct lock cache_lock;   /* Lock for Buffer Cache */
struct lock disk_lock;    /* Lock for File Disk */

struct lock insert_lock;

static struct cache_entry * cache_insert (disk_sector_t sec_no);
static bool cache_evict (void);

/* Initializes the buffer cache (called in threads/init.c) */
void
cache_init (void)
{
  list_init (&buffer_cache);
  lock_init (&cache_lock);
  lock_init (&disk_lock);
  lock_init (&insert_lock);

  //hash_init (&buffer_cache, cache_hash, cache_less, NULL);
  return;
}

static struct cache_entry *
cache_insert (disk_sector_t sec_no)
{
  lock_acquire (&insert_lock);

  if (list_size (&buffer_cache) >= CACHE_SIZE_LIMIT)
  {
    ASSERT(cache_evict());
  }

	struct cache_entry *c = malloc(sizeof *c);

  c -> sec_no = sec_no;
  c -> dirty = false;
  c -> access = false;

  lock_acquire (&disk_lock);
  disk_read (filesys_disk, sec_no, &c->block);
  lock_release (&disk_lock);

	lock_acquire(&cache_lock);
	list_push_back (&buffer_cache, &c->elem);
  lock_release(&cache_lock);

  lock_release (&insert_lock);

  return c;
}

static bool
cache_evict (void)
{
  ASSERT(!list_empty(&buffer_cache));

  struct cache_entry *c = list_entry (list_begin (&buffer_cache), struct cache_entry, elem);
  struct cache_entry *c_next;

  /* cache replacement policy: second chance algorithm */
  while (&c_next->elem != list_end(&buffer_cache))
  {
    c_next = list_entry (list_next (&c->elem), struct cache_entry, elem);
    
    if (c->access == true)
    {
      c->access = false;
      list_remove (&c->elem);
      list_push_back (&buffer_cache, &c->elem);
    } else {
      break;
    }
  }

  if (c->dirty)
  {
    /* write back */
    lock_acquire (&disk_lock);
    disk_write (filesys_disk, c->sec_no, &c->block);
    lock_release (&disk_lock);
  }

  list_remove (&c->elem);
  free(c);

  return true;
}

struct cache_entry *
cache_find (disk_sector_t sec_no)
{
  lock_acquire(&cache_lock);

  struct list_elem *e;
  struct cache_entry *c;

  for (e = list_begin (&buffer_cache); e != list_end (&buffer_cache); e = list_next (e))
  {
    c = list_entry (e, struct cache_entry, elem);
    if (c->sec_no == sec_no)
    {
      lock_release(&cache_lock);
      return c;
    }
  }

  lock_release(&cache_lock);

  c = cache_insert (sec_no);
  ASSERT(c != NULL);

  return c;
}

bool
cache_write (disk_sector_t sec_no, void* buffer, int ofs, int size)
{
  struct cache_entry *c = cache_find (sec_no);
  ASSERT(c!=NULL);

  memcpy ((uint8_t *) &c->block + ofs, buffer, size);
  c->dirty = true;
  c->access = true;
  return true;
}

bool
cache_read (disk_sector_t sec_no, void* buffer, int ofs, int size)
{
  struct cache_entry *c = cache_find (sec_no);
  ASSERT(c!=NULL);

  memcpy (buffer, (uint8_t *) &c->block + ofs, size);
  c->access = true;
  return true;
}

void
cache_done (void)
{
  if(list_empty(&buffer_cache))
    return true;

  struct list_elem *e;
  struct cache_entry *c;

  for (e = list_begin (&buffer_cache); e != list_end (&buffer_cache); e = list_next (e))
  {
    c = list_entry (e, struct cache_entry, elem);
    if (c->dirty)
    {
      /* write back */
      lock_acquire (&disk_lock);
      disk_write (filesys_disk, c->sec_no, &c->block);
      lock_release (&disk_lock);
    }
  }

  return true;
}














/* Hash table help funtions */

/*

static void cache_destroy (struct hash *hash);
static void cache_create (struct hash *hash);
static unsigned cache_hash (const struct hash_elem *p_, void *aux UNUSED);
static bool cache_less (const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED);


static unsigned
cache_hash (const struct hash_elem *c_, void *aux UNUSED)
{
  const struct cache_entry *c = hash_entry (c_, struct cache_entry, elem);
  return hash_bytes (&c->sec_no, sizeof c->sec_no);
}

static bool
cache_less (const struct hash_elem *a_, const struct hash_elem *b_,
                void *aux UNUSED)
{
  const struct cache_entry *a = hash_entry (a_, struct cache_entry, elem);
  const struct cache_entry *b = hash_entry (b_, struct cache_entry, elem);
  return a->sec_no < b->sec_no;
}

static void
cache_create (struct hash *hash)
{
  hash_init (hash, cache_hash, cache_less, NULL);
}

static void
cache_destroy (struct hash *hash)
{
  // hash_destroy (struct hash *hash, hash action func *action);
  return;
}
*/
