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
static void filename_tokenize(const char *path, char *directory, char *filename);

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

  char *directory = malloc(sizeof(char) * strlen(name));
  char *file_name = malloc(sizeof(char) * strlen(name));
  filename_tokenize(name, directory, file_name);
  //printf("directory: %s, filename: %s\n", directory, file_name);

  struct dir *dir = dir_open_path (directory);

  bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size, is_dir)
                  && dir_add (dir, file_name, inode_sector, is_dir));
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

  char *directory = malloc(sizeof(char) * strlen(name));
  char *file_name = malloc(sizeof(char) * strlen(name));
  filename_tokenize(name, directory, file_name);
  //printf("directory: %s, filename: %s\n", directory, file_name);

  struct dir *dir = dir_open_path (directory);
  if (dir == NULL)
  {
    //printf("open path: null\n");
    return NULL;
  }

  struct inode *inode = NULL;

  if (strlen(file_name) > 0)
  {
    //printf("lets dir lookup2\n");
    dir_lookup (dir, file_name, &inode);
    dir_close (dir);
  }
  else { // empty filename : just return the directory
    inode = dir_get_inode (dir);
  }

  if (inode == NULL || inode_is_removed (inode))
  {
    //printf("filesys open inode null\n");
    return NULL;
  }

  return file_open (inode);
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

  char *directory = malloc(sizeof(char) * strlen(name));
  char *file_name = malloc(sizeof(char) * strlen(name));
  filename_tokenize(name, directory, file_name);
  struct dir *dir = dir_open_path (directory);

  bool success = (dir != NULL && dir_remove (dir, file_name));

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
filename_tokenize(const char *path, char *directory, char *filename)
{  
  int path_length = path_length = strlen(path);
  int filename_length = path_length;

  char *path_temp = (char*) malloc(sizeof(char) * (strlen(path)+1));
  memcpy (path_temp, path, sizeof(char) * (strlen(path)+1));

  if (!strcmp(path, (char *)""))
  {
    *directory = '\0';
    *filename = '\0';
    return;
  }

  if (!strcmp(path, (char *)"/"))
  {
    char *directory_pos = directory;
    *directory_pos++ = '/';
    *directory_pos = '\0';
    *filename = '\0';
    return;
  }

  char *token, *save_ptr;
  for (token = strtok_r(path_temp, "/", &save_ptr); token != NULL;
       token = strtok_r(NULL, "/", &save_ptr))
  {
    filename_length = strlen (token);
  }

  strlcpy(directory, path, path_length - filename_length + 1);
  char *end_char = "\0";
  strlcat(directory, end_char, (sizeof *end_char));
  strlcpy(filename, path + path_length - filename_length, filename_length + 2);

  return;
}

/*------------- SYSTEM CALL helper function ----------------*/

bool
filesys_chdir (const char *name)
{
  //printf("filesys chdir enter %s\n", name);
  struct thread *curr = thread_current();
  struct dir *chdir = dir_open_path (name);

  if(chdir == NULL)
  {
    //printf("filesys chdir NULL\n");
    return false;
  }

  dir_close (curr->cwd);
  curr->cwd = chdir;
  //printf("filesys chdir exit\n");
  return true;
}
