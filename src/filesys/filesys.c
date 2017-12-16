#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "filesys/cache.h"
#include "devices/disk.h"

#include "threads/thread.h"

/* The disk that contains the file system. */
struct disk *filesys_disk;

static void do_format (void);
static void filename_tokenize(const char *name, char *token_dir, char *token_file);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) 
{
  filesys_disk = disk_get (0, 1);
  if (filesys_disk == NULL)
    PANIC ("hd0:1 (hdb) not present, file system initialization failed");

  inode_init ();
  free_map_init ();
  cache_init ();

  if (format) 
    do_format ();

  free_map_open ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  free_map_close ();
  cache_done ();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size, bool is_dir) 
{
  disk_sector_t inode_sector = 0;

  char *token_dir = malloc(sizeof(char) * strlen(name));
  char *token_file = malloc(sizeof(char) * strlen(name));
  filename_tokenize(name, token_dir, token_file);
  //printf("token_dir: %s, filename: %s\n", token_dir, file_name);

  struct dir *dir = dir_open_path (token_dir);

  bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size, is_dir)
                  && dir_add (dir, token_file, inode_sector, is_dir));
  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);
  dir_close (dir);

  //printf("filesys create return %d\n", success);
  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
  //struct dir *dir = dir_open_root ();
  //printf("filesys open %s\n", name);

  char *token_dir = malloc(sizeof(char) * strlen(name));
  char *token_file = malloc(sizeof(char) * strlen(name));
  filename_tokenize(name, token_dir, token_file);
  //printf("directory: %s, filename: %s\n", directory, file_name);

  struct dir *dir = dir_open_path (token_dir);
  struct inode *inode = NULL;

  if (dir != NULL)
  {
    if (strlen(token_file) > 0)
    {
      //printf("lets dir lookup2\n");
      dir_lookup (dir, token_file, &inode);
      dir_close (dir);
    } else {
      inode = dir_get_inode (dir);
    }

    if (inode == NULL) 
      return NULL;
    if(inode_is_removed (inode))
      return NULL;
  
    return file_open (inode);
  }
  return NULL;
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) 
{
  //struct dir *dir = dir_open_root ();
  //bool success = dir != NULL && dir_remove (dir, name);

  char *token_dir = malloc(sizeof(char) * strlen(name));
  char *token_file = malloc(sizeof(char) * strlen(name));
  filename_tokenize(name, token_dir, token_file);

  struct dir *dir = dir_open_path (token_dir);

  bool success = dir != NULL && dir_remove (dir, token_file);
  dir_close (dir); 

  return success;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}

static void
filename_tokenize(const char *name, char *token_dir, char *token_file)
{  
  int path_length = path_length = strlen(name);
  int filename_length = path_length;

  char *path_temp = (char*) malloc(sizeof(char) * (strlen(name)+1));
  memcpy (path_temp, name, sizeof(char) * (strlen(name)+1));

  if (!strcmp(name, (char *)""))
  {
    *token_dir = '\0';
    *token_file = '\0';
    return;
  }

  if (!strcmp(name, (char *)"/"))
  {
    char *directory_pos = token_dir;
    *directory_pos++ = '/';
    *directory_pos = '\0';
    *token_file = '\0';
    return;
  }

  char *token, *save_ptr;
  for (token = strtok_r(path_temp, "/", &save_ptr); token != NULL;
       token = strtok_r(NULL, "/", &save_ptr))
  {
    filename_length = strlen (token);
  }

  strlcpy(token_dir, name, path_length - filename_length + 1);
  char *end_char = "\0";
  strlcat(token_dir, end_char, (sizeof *end_char));
  strlcpy(token_file, name + path_length - filename_length, filename_length + 2);

  return;
}

/*------------- SYSTEM CALL helper function ----------------*/

static void cwd_switch (struct dir *dir);

bool
filesys_chdir (const char *name)
{
  //printf("filesys chdir enter %s\n", name);
  struct thread *curr = thread_current();
  struct dir *chdir = dir_open_path (name);

  if(chdir != NULL)
  {
    cwd_switch (chdir);
    return true;
  }
  return false;
}

static void
cwd_switch (struct dir *dir)
{
  struct thread *curr = thread_current();
  dir_close (curr->cwd);
  curr->cwd = dir;
}
