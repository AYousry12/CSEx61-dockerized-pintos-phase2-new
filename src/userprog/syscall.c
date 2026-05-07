#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "devices/shutdown.h"
#include "devices/input.h"

static void syscall_handler (struct intr_frame *);

/* Prototypes for individual syscall implementations. */
static void     sys_halt   (void);
static void     sys_exit   (int status);
static tid_t    sys_exec   (const char *cmd_line);
static int      sys_wait   (tid_t pid);
static bool     sys_create (const char *file, unsigned initial_size);
static bool     sys_remove (const char *file);
static int      sys_open   (const char *file);
static int      sys_filesize (int fd);
static int      sys_read   (int fd, void *buffer, unsigned size);
static int      sys_write  (int fd, const void *buffer, unsigned size);
static void     sys_seek   (int fd, unsigned position);
static unsigned sys_tell   (int fd);
static void     sys_close  (int fd);

/* Helpers. */
static void check_valid_ptr (const void *ptr);
static void check_valid_string (const void *str);
static void check_valid_buffer (const void *buf, unsigned size);
static struct file *fd_to_file (int fd);

/* Global filesystem lock — Pintos filesys is not thread-safe. */
static struct lock filesys_lock;

void
syscall_init (void)
{
  lock_init (&filesys_lock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f)
{
  uint32_t *args = (uint32_t *) f->esp;

  /* Validate the full 4-byte word for the syscall number. */
  check_valid_ptr ((const void *) args);
  check_valid_ptr ((const void *) ((uint8_t *) args + 3));

  int syscall_number = (int) args[0];

  switch (syscall_number)
    {
    case SYS_HALT:
      sys_halt ();
      break;

    case SYS_EXIT:
      check_valid_ptr ((const void *) (args + 1));
      check_valid_ptr ((const void *) ((uint8_t *) (args + 1) + 3));
      sys_exit ((int) args[1]);
      break;

    case SYS_EXEC:
      check_valid_ptr ((const void *) (args + 1));
      check_valid_ptr ((const void *) ((uint8_t *) (args + 1) + 3));
      check_valid_string ((const char *) args[1]);
      f->eax = (uint32_t) sys_exec ((const char *) args[1]);
      break;

    case SYS_WAIT:
      check_valid_ptr ((const void *) (args + 1));
      check_valid_ptr ((const void *) ((uint8_t *) (args + 1) + 3));
      f->eax = (uint32_t) sys_wait ((tid_t) args[1]);
      break;

    case SYS_CREATE:
      check_valid_ptr ((const void *) (args + 1));
      check_valid_ptr ((const void *) ((uint8_t *) (args + 2) + 3));
      check_valid_string ((const char *) args[1]);
      f->eax = (uint32_t) sys_create ((const char *) args[1],
                                       (unsigned) args[2]);
      break;

    case SYS_REMOVE:
      check_valid_ptr ((const void *) (args + 1));
      check_valid_ptr ((const void *) ((uint8_t *) (args + 1) + 3));
      check_valid_string ((const char *) args[1]);
      f->eax = (uint32_t) sys_remove ((const char *) args[1]);
      break;

    case SYS_OPEN:
      check_valid_ptr ((const void *) (args + 1));
      check_valid_ptr ((const void *) ((uint8_t *) (args + 1) + 3));
      check_valid_string ((const char *) args[1]);
      f->eax = (uint32_t) sys_open ((const char *) args[1]);
      break;

    case SYS_FILESIZE:
      check_valid_ptr ((const void *) (args + 1));
      check_valid_ptr ((const void *) ((uint8_t *) (args + 1) + 3));
      f->eax = (uint32_t) sys_filesize ((int) args[1]);
      break;

    case SYS_READ:
      check_valid_ptr ((const void *) (args + 1));
      check_valid_ptr ((const void *) ((uint8_t *) (args + 3) + 3));
      check_valid_buffer ((const void *) args[2], (unsigned) args[3]);
      f->eax = (uint32_t) sys_read ((int) args[1], (void *) args[2],
                                     (unsigned) args[3]);
      break;

    case SYS_WRITE:
      check_valid_ptr ((const void *) (args + 1));
      check_valid_ptr ((const void *) ((uint8_t *) (args + 3) + 3));
      check_valid_buffer ((const void *) args[2], (unsigned) args[3]);
      f->eax = (uint32_t) sys_write ((int) args[1], (const void *) args[2],
                                      (unsigned) args[3]);
      break;

    case SYS_SEEK:
      check_valid_ptr ((const void *) (args + 1));
      check_valid_ptr ((const void *) ((uint8_t *) (args + 2) + 3));
      sys_seek ((int) args[1], (unsigned) args[2]);
      break;

    case SYS_TELL:
      check_valid_ptr ((const void *) (args + 1));
      check_valid_ptr ((const void *) ((uint8_t *) (args + 1) + 3));
      f->eax = (uint32_t) sys_tell ((int) args[1]);
      break;

    case SYS_CLOSE:
      check_valid_ptr ((const void *) (args + 1));
      check_valid_ptr ((const void *) ((uint8_t *) (args + 1) + 3));
      sys_close ((int) args[1]);
      break;

    default:
      sys_exit (-1);
      break;
    }
}

/* ----------------------------------------------------------------
   Pointer / memory validation helpers
   ---------------------------------------------------------------- */

static void
check_valid_ptr (const void *ptr)
{
  if (ptr == NULL || !is_user_vaddr (ptr)
      || pagedir_get_page (thread_current ()->pagedir, ptr) == NULL)
    sys_exit (-1);
}

static void
check_valid_string (const void *str)
{
  check_valid_ptr (str);
  const char *s = (const char *) str;
  while (*s != '\0')
    {
      s++;
      check_valid_ptr ((const void *) s);
    }
}

static void
check_valid_buffer (const void *buf, unsigned size)
{
  const uint8_t *p = (const uint8_t *) buf;
  unsigned i;
  for (i = 0; i < size; i++)
    check_valid_ptr ((const void *) (p + i));
}

/* Return the struct file * for a given fd, or NULL if invalid. */
static struct file *
fd_to_file (int fd)
{
  if (fd < 2 || fd >= MAX_FD)
    return NULL;
  return thread_current ()->fd_table[fd];
}

/* ----------------------------------------------------------------
   Syscall implementations
   ---------------------------------------------------------------- */

static void
sys_halt (void)
{
  shutdown_power_off ();
}

static void
sys_exit (int status)
{
  struct thread *cur = thread_current ();
  cur->exit_status = status;
  thread_exit ();
}

static tid_t
sys_exec (const char *cmd_line)
{
  return process_execute (cmd_line);
}

static int
sys_wait (tid_t pid)
{
  return process_wait (pid);
}

static bool
sys_create (const char *file, unsigned initial_size)
{
  lock_acquire (&filesys_lock);
  bool ok = filesys_create (file, initial_size);
  lock_release (&filesys_lock);
  return ok;
}

static bool
sys_remove (const char *file)
{
  lock_acquire (&filesys_lock);
  bool ok = filesys_remove (file);
  lock_release (&filesys_lock);
  return ok;
}

static int
sys_open (const char *file)
{
  lock_acquire (&filesys_lock);
  struct file *f = filesys_open (file);
  lock_release (&filesys_lock);

  if (f == NULL)
    return -1;

  struct thread *cur = thread_current ();
  int fd = cur->next_fd;
  if (fd >= MAX_FD)
    {
      file_close (f);
      return -1;
    }
  cur->fd_table[fd] = f;
  cur->next_fd++;
  return fd;
}

static int
sys_filesize (int fd)
{
  struct file *f = fd_to_file (fd);
  if (f == NULL)
    return -1;
  lock_acquire (&filesys_lock);
  int size = file_length (f);
  lock_release (&filesys_lock);
  return size;
}

static int
sys_read (int fd, void *buffer, unsigned size)
{
  if (fd == 0)
    {
      /* Read from stdin. */
      uint8_t *buf = (uint8_t *) buffer;
      unsigned i;
      for (i = 0; i < size; i++)
        buf[i] = input_getc ();
      return (int) size;
    }

  struct file *f = fd_to_file (fd);
  if (f == NULL)
    return -1;

  lock_acquire (&filesys_lock);
  int bytes = file_read (f, buffer, size);
  lock_release (&filesys_lock);
  return bytes;
}

static int
sys_write (int fd, const void *buffer, unsigned size)
{
  if (fd == 1)
    {
      /* Write to stdout. */
      putbuf (buffer, size);
      return (int) size;
    }

  struct file *f = fd_to_file (fd);
  if (f == NULL)
    return -1;

  lock_acquire (&filesys_lock);
  int bytes = file_write (f, buffer, size);
  lock_release (&filesys_lock);
  return bytes;
}

static void
sys_seek (int fd, unsigned position)
{
  struct file *f = fd_to_file (fd);
  if (f == NULL)
    return;
  lock_acquire (&filesys_lock);
  file_seek (f, position);
  lock_release (&filesys_lock);
}

static unsigned
sys_tell (int fd)
{
  struct file *f = fd_to_file (fd);
  if (f == NULL)
    return 0;
  lock_acquire (&filesys_lock);
  unsigned pos = (unsigned) file_tell (f);
  lock_release (&filesys_lock);
  return pos;
}

static void
sys_close (int fd)
{
  struct file *f = fd_to_file (fd);
  if (f == NULL)
    return;
  thread_current ()->fd_table[fd] = NULL;
  lock_acquire (&filesys_lock);
  file_close (f);
  lock_release (&filesys_lock);
}
