#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include <stdio.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "filesys/cache.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

/* Amount of sector_t in block */
#define BLOCK_CAP 128

/* On-disk inode.
   Must be exactly DISK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    disk_sector_t start;                /* Level 1 indirect inode sector. */
    off_t length;                       /* File size in bytes. */
    bool is_dir;
    unsigned magic;                     /* Magic number. */
    uint32_t unused[124];               /* Not used. */
  };

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, DISK_SECTOR_SIZE);
}

struct indir_block{ disk_sector_t ptr[BLOCK_CAP]; };

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    disk_sector_t sector;               /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct inode_disk data;             /* Inode content. */
    struct lock inode_lock;
  };

/* Returns the disk sector that contains byte offset POS within
   INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static disk_sector_t
byte_to_sector (const struct inode *inode, off_t pos) 
{
  ASSERT (inode != NULL);
  size_t index1, index2;
  if( pos < inode_length(inode) ){
    struct indir_block* level1 = (struct indir_block*)malloc(sizeof(struct indir_block));
    struct indir_block* level2 = (struct indir_block*)malloc(sizeof(struct indir_block));
    if( level1 == NULL ) return -1;
    if( level2 == NULL ){
      free(level1);
      return -1;
    }
    index1 = ( pos / DISK_SECTOR_SIZE ) / BLOCK_CAP;
    index2 = ( pos / DISK_SECTOR_SIZE ) % BLOCK_CAP;
    disk_read (filesys_disk, inode->data.start, level1);
    disk_read (filesys_disk, level1->ptr[index1], level2);
    disk_sector_t result = level2->ptr[index2];
    free(level1);
    free(level2);
    return result;
  }
  else return -1;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   disk.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (disk_sector_t sector, off_t length, bool is_dir)
{
  struct inode_disk *disk_inode = NULL;
  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == DISK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode == NULL) return false;
  size_t sectors = bytes_to_sectors (length);
  disk_inode->length = length;
  disk_inode->magic = INODE_MAGIC;
  disk_inode->is_dir = is_dir;

  if (!free_map_allocate (1, &disk_inode->start))
    return false; // allocate first level inode ptr

  struct indir_block* level1 = (struct indir_block*)malloc(sizeof(struct indir_block));
  struct indir_block* level2 = (struct indir_block*)malloc(sizeof(struct indir_block));
  if( level1 == NULL ) return -1;
  if( level2 == NULL ){
    free(level1);
    return -1;
  }
  size_t i;
  for( i = 0; i < sectors; i++ ){
    if ( i % BLOCK_CAP == 0 ){
      if(!free_map_allocate (1, &level1->ptr[i/BLOCK_CAP]) )
        return false; // allocate second level inode ptr
    }
    if( !free_map_allocate (1, &level2->ptr[i%BLOCK_CAP]) )
      return false; // allocate data sector
    static char zeros[DISK_SECTOR_SIZE];
    disk_write (filesys_disk, level2->ptr[i%BLOCK_CAP], zeros);
    // write data to disk
    if( i % BLOCK_CAP == BLOCK_CAP-1 ){
      disk_write(filesys_disk, level1->ptr[i/BLOCK_CAP], level2 );
      //write level 2 to disk
      memset( level2, 0, sizeof(struct indir_block) );
      //memset level 2 to zero
    }
  }
  if( i % BLOCK_CAP != 0 ){
    disk_write(filesys_disk, level1->ptr[i/BLOCK_CAP], level2 );
    //write remain level 2 to disk
  }
  disk_write (filesys_disk, disk_inode->start, level1);
  // write level 1 to disk
  disk_write (filesys_disk, sector, disk_inode);
  // write inode_disk to disk
  free(disk_inode);
  free(level1);
  free(level2);
  return true;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (disk_sector_t sector) 
{
  struct list_elem *e;
  struct inode *inode;
  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          return inode; 
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  lock_init(&inode->inode_lock);
  disk_read (filesys_disk, inode->sector, &inode->data);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
disk_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0){
    /* Remove from inode list and release lock. */
    list_remove (&inode->elem);

    /* Deallocate blocks if removed. */
    if (inode->removed){
      size_t i;
      struct indir_block* level1 = (struct indir_block*)malloc(sizeof(struct indir_block));
      struct indir_block* level2 = (struct indir_block*)malloc(sizeof(struct indir_block));
      if( level1 == NULL ) return -1;
      if( level2 == NULL ){
        free(level1);
        return;
      }

      disk_read (filesys_disk, inode->data.start, level1);
      for( i = 0 ; i < bytes_to_sectors( inode_length(inode) ); i++){
        if( i % BLOCK_CAP == 0  )
          disk_read (filesys_disk, level1->ptr[i / BLOCK_CAP], level2);
        free_map_release (level2->ptr[i % BLOCK_CAP], 1);
        if( i % BLOCK_CAP == BLOCK_CAP - 1 ){
          free_map_release (level1->ptr[i / BLOCK_CAP], 1);
          memset( level2, 0, sizeof(struct indir_block) );
        }
      }
      if( i % BLOCK_CAP != 0 )
        free_map_release (level1->ptr[i / BLOCK_CAP], 1);
      // Deallocate remain block
      free_map_release (inode->data.start, 1);
      free_map_release (inode->sector, 1);
    }
    free (inode); 
  }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset){
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;

  while (size > 0){
    /* Disk sector to read, starting byte offset within sector. */
    disk_sector_t sector_idx = byte_to_sector (inode, offset);
    int sector_ofs = offset % DISK_SECTOR_SIZE;

    /* Bytes left in inode, bytes left in sector, lesser of the two. */
    off_t inode_left = inode_length (inode) - offset;
    if( inode_left < 0 ) return bytes_read;
    int sector_left = DISK_SECTOR_SIZE - sector_ofs;
    int min_left = inode_left < sector_left ? inode_left : sector_left;

    /* Number of bytes to actually copy out of this sector. */
    int chunk_size = size < min_left ? size : min_left;
    if (chunk_size <= 0)
      break;
    
    cache_read (sector_idx, buffer + bytes_read, sector_ofs, chunk_size);

    /* Advance. */
    size -= chunk_size;
    offset += chunk_size;
    bytes_read += chunk_size;
  }
  return bytes_read;
}


static bool inode_write_expand(struct inode *inode, off_t length){
  // Expanding sequence
  ASSERT( inode_length(inode) < length );
  int i = (inode_length(inode)-1) / DISK_SECTOR_SIZE + 1;
  if(inode_length(inode) == 0) i = 0;
  struct indir_block *level1;
  struct indir_block *level2;

  level1 = (struct indir_block*)malloc(sizeof(struct indir_block));
  level2 = (struct indir_block*)malloc(sizeof(struct indir_block));
  if( level1 == NULL ) return false;
  else if( level2 == NULL ){
    free(level1);
    return false;
  }
  disk_read(filesys_disk, inode->data.start, level1);
  if( i % BLOCK_CAP != 0){
    disk_read(filesys_disk, level1->ptr[i/BLOCK_CAP], level2);
  }

  for( ; i < (length-1)/DISK_SECTOR_SIZE + 1; i++ ){
    if ( i % BLOCK_CAP == 0 ){
      if(!free_map_allocate (1, &level1->ptr[i/BLOCK_CAP]) )
        return false; // allocate second level inode ptr
    }
    if( !free_map_allocate (1, &level2->ptr[i%BLOCK_CAP]) )
      return false; // allocate data sector
    static char zeros[DISK_SECTOR_SIZE];
    disk_write (filesys_disk, level2->ptr[i%BLOCK_CAP], zeros);
    // write data to disk
    if( i % BLOCK_CAP == BLOCK_CAP-1 ){
      disk_write(filesys_disk, level1->ptr[i/BLOCK_CAP], level2 );
      //write level 2 to disk
      memset( level2, 0, sizeof(struct indir_block) );
      //memset level 2 to zero
    }
  }
  if( i % BLOCK_CAP != 0 ){
    disk_write(filesys_disk, level1->ptr[i/BLOCK_CAP], level2 );
    //write remain level 2 to disk
  }
  disk_write (filesys_disk, inode->data.start, level1);
  // write level 1 to disk
  inode->data.length = length;
  disk_write (filesys_disk, inode->sector, &inode->data);
  // write inode_disk to disk
  free(level1);
  free(level2);
  return true;
}


/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  if (inode->deny_write_cnt)
    return 0;
  
  if ( (size + offset) > inode_length(inode)){
    lock_acquire(&inode->inode_lock);
    if( !inode_write_expand(inode, size+offset) ){
      lock_release(&inode->inode_lock);
      return 0;
    }
    lock_release(&inode->inode_lock);
  }

  while (size > 0){
    
    /* Sector to write, starting byte offset within sector. */
    disk_sector_t sector_idx = byte_to_sector (inode, offset);
    int sector_ofs = offset % DISK_SECTOR_SIZE;

    /* Bytes left in inode, bytes left in sector, lesser of the two. */
    off_t inode_left = inode_length (inode) - offset;
    int sector_left = DISK_SECTOR_SIZE - sector_ofs;
    int min_left = inode_left < sector_left ? inode_left : sector_left;

    /* Number of bytes to actually write into this sector. */
    int chunk_size = size < min_left ? size : min_left;
    if (chunk_size <= 0)
      break;

    cache_write (sector_idx, (void*) buffer + bytes_written, sector_ofs, chunk_size);

    /* Advance. */
    size -= chunk_size;
    offset += chunk_size;
    bytes_written += chunk_size;
  }

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->data.length;
}

bool
inode_is_directory (const struct inode *inode)
{
  return inode->data.is_dir;
}

bool
inode_is_removed (const struct inode *inode)
{
  return inode->removed;
}

