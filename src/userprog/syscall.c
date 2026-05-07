#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "devices/shutdown.h"
#include "devices/input.h"
#include "userprog/process.h"

static void syscall_handler(struct intr_frame *);
void halt(void);
void exit(int status);
tid_t exec(const char *cmd_line);
int wait(tid_t pid);
void check_valid_ptr(const void *ptr);
void check_valid_string(const void *str);
int open(const char *file);
int filesize(int fd);
int read(int fd, void *buffer, unsigned size);
int write(int fd, const void *buffer, unsigned size);
void seek(int fd, unsigned position);
int tell(int fd);
void close(int fd);

static struct lock filesys_lock;
void syscall_init(void)
{
  lock_init(&filesys_lock);
  intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler(struct intr_frame *f UNUSED)
{
  uint32_t *args = (uint32_t *) f->esp;

  check_valid_ptr((const void *) args);
  check_valid_ptr((const void *) ((uint8_t *) args + 3));

  int syscall_number = args[0];

  switch (syscall_number)
  {
  case SYS_HALT:
    halt();
    break;

  case SYS_EXIT:
    check_valid_ptr((const void *)(args + 1));
    check_valid_ptr((const void *)((uint8_t *)(args + 1) + 3));
    exit((int)args[1]);
    break;

  case SYS_EXEC:
    check_valid_ptr((const void *)(args + 1));
    check_valid_ptr((const void *)((uint8_t *)(args + 1) + 3));
    check_valid_string((const char *)args[1]);
    f->eax = exec((const char *)args[1]);
    break;

  case SYS_WAIT:
    check_valid_ptr((const void *)(args + 1));
    check_valid_ptr((const void *)((uint8_t *)(args + 1) + 3));
    f->eax = wait((tid_t)args[1]);
    break;

  case SYS_CREATE:
    check_valid_ptr((const void *)(args + 1));
    check_valid_ptr((const void *)((uint8_t *)(args + 2) + 3));
    check_valid_string((const char *)args[1]);
    lock_acquire(&filesys_lock);
    f->eax = filesys_create((const char *)args[1], (unsigned)args[2]);
    lock_release(&filesys_lock);
    break;

  case SYS_REMOVE:
    check_valid_ptr((const void *)(args + 1));
    check_valid_ptr((const void *)((uint8_t *)(args + 1) + 3));
    check_valid_string((const char *)args[1]);
    lock_acquire(&filesys_lock);
    f->eax = filesys_remove((const char *)args[1]);
    lock_release(&filesys_lock);
    break;

  case SYS_OPEN:
    check_valid_ptr((const void *)(args + 1));
    check_valid_ptr((const void *)((uint8_t *)(args + 1) + 3));
    check_valid_string((const char *)args[1]);
    f->eax = open((const char *)args[1]);
    break;

  case SYS_FILESIZE:
    check_valid_ptr((const void *)(args + 1));
    check_valid_ptr((const void *)((uint8_t *)(args + 1) + 3));
    f->eax = filesize((int)args[1]);
    break;

  case SYS_READ:
    check_valid_ptr((const void *)(args + 1));
    check_valid_ptr((const void *)((uint8_t *)(args + 3) + 3));
    f->eax = read((int)args[1], (void *)args[2], (unsigned)args[3]);
    break;

  case SYS_WRITE:
    check_valid_ptr((const void *)(args + 1));
    check_valid_ptr((const void *)((uint8_t *)(args + 3) + 3));
    f->eax = write((int)args[1], (const void *)args[2], (unsigned)args[3]);
    break;

  case SYS_SEEK:
    check_valid_ptr((const void *)(args + 1));
    check_valid_ptr((const void *)((uint8_t *)(args + 2) + 3));
    seek((int)args[1], (unsigned)args[2]);
    break;

  case SYS_TELL:
    check_valid_ptr((const void *)(args + 1));
    check_valid_ptr((const void *)((uint8_t *)(args + 1) + 3));
    f->eax = tell((int)args[1]);
    break;

  case SYS_CLOSE:
    check_valid_ptr((const void *)(args + 1));
    check_valid_ptr((const void *)((uint8_t *)(args + 1) + 3));
    close((int)args[1]);
    break;

  default:
    exit(-1);
    break;
  }
}
void halt()
{
  shutdown_power_off();
}
void exit(int status)
{
  struct thread *cur = thread_current();
  cur->exit_status = status;
  thread_exit();
}
tid_t exec(const char *cmd_line)
{
  check_valid_string(cmd_line);
  return process_execute(cmd_line);
}
int wait(tid_t pid)
{
  return process_wait(pid);
}

void check_valid_ptr(const void *ptr)
{
  if (ptr == NULL)
  {
    exit(-1);
  }
  if (!is_user_vaddr(ptr))
  {
    exit(-1);
  }
  void *page_ptr = pagedir_get_page(thread_current()->pagedir, ptr);
  if (page_ptr == NULL)
  {
    exit(-1);
  }
}

void check_valid_string(const void *str)
{
  check_valid_ptr(str);
  char *ptr = (char *)str;
  while (*ptr != '\0')
  {
    ptr++;
    check_valid_ptr(ptr);
  }
}
int open(const char *file)
{
  check_valid_string(file);
  lock_acquire(&filesys_lock);
  struct file *f = filesys_open(file);
  lock_release(&filesys_lock);
  if (f == NULL)
    return -1;

  struct thread *cur = thread_current();
  int fd = cur->next_fd;
  if (fd >= MAX_FD) {
    file_close(f);
    return -1;
  }
  cur->fd_table[fd] = f;
  cur->next_fd++;
  return fd;
}

int filesize(int fd)
{
  if (fd < 2 || fd >= MAX_FD || thread_current()->fd_table[fd] == NULL)
    return -1;
  lock_acquire(&filesys_lock);
  int size = file_length(thread_current()->fd_table[fd]);
  lock_release(&filesys_lock);
  return size;
}
int read(int fd, void *buffer, unsigned size)
{
  check_valid_ptr(buffer);
  check_valid_ptr(buffer + size - 1);
  if (fd == 0) {
    uint8_t *buf = (uint8_t *) buffer;
    unsigned i;
    for (i = 0; i < size; i++)
      buf[i] = input_getc();
    return size;
  }
  if (fd < 2 || fd >= MAX_FD || thread_current()->fd_table[fd] == NULL)
    return -1;
  lock_acquire(&filesys_lock);
  int bytes = file_read(thread_current()->fd_table[fd], buffer, size);
  lock_release(&filesys_lock);
  return bytes;
}
int write(int fd, const void *buffer, unsigned size)
{
  check_valid_ptr(buffer);
  check_valid_ptr(buffer + size - 1);
  if (fd == 1) {
    putbuf(buffer, size);
    return size;
  }
  if (fd < 2 || fd >= MAX_FD || thread_current()->fd_table[fd] == NULL)
    return -1;
  lock_acquire(&filesys_lock);
  int bytes = file_write(thread_current()->fd_table[fd], buffer, size);
  lock_release(&filesys_lock);
  return bytes;
}
void seek(int fd, unsigned position)
{
  if (fd < 2 || fd >= MAX_FD || thread_current()->fd_table[fd] == NULL)
    return;
  lock_acquire(&filesys_lock);
  file_seek(thread_current()->fd_table[fd], position);
  lock_release(&filesys_lock);
}
int tell(int fd)
{
  if (fd < 2 || fd >= MAX_FD || thread_current()->fd_table[fd] == NULL)
    return -1;
  lock_acquire(&filesys_lock);
  int pos = file_tell(thread_current()->fd_table[fd]);
  lock_release(&filesys_lock);
  return pos;
}
void close(int fd)
{
  if (fd < 2 || fd >= MAX_FD || thread_current()->fd_table[fd] == NULL)
    return;
  lock_acquire(&filesys_lock);
  file_close(thread_current()->fd_table[fd]);
  lock_release(&filesys_lock);
  thread_current()->fd_table[fd] = NULL;
}
