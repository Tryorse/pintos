#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"

static void syscall_handler (struct intr_frame *);
static void validate_pointer (const void *user_address);
static void validate_buffer (void *buffer, unsigned size);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void validate_buffer (void *buffer, unsigned size)
{
  if (size == 0)
    return;

  char *start = (char *)buffer;
  char *end = start + size - 1;

  validate_pointer(start);
  validate_pointer(end);

  for (char *p = pg_round_down(start); p <= pg_round_down(end); p += PGSIZE)
  {
    validate_pointer(p);
  }
}

//if the pointer sent in is null, not a user virtual address
static void validate_pointer (const void *user_address)
{
  if (user_address == NULL || !is_user_vaddr(user_address) || pagedir_get_page(thread_current()->pagedir, user_address) == NULL)
  {
    thread_current()->exitStatus = -1;
    thread_exit();
  }
}

//this is necessary because validate_pointer() only checks if the starting address has been mapped. Meaning that not every byte got validated as being in mapped memory
static void validate_read(const void *addr, size_t size)
{
  for (size_t i = 0; i < size; i++)
  {
      validate_pointer((const char *)addr + i);
  }
}

static void
syscall_handler (struct intr_frame *f) 
{
  //validate the stack pointer that is to be grabbeds
  validate_read(f->esp, 4);
  int syscall_num = *(int *)(f->esp);

  switch (syscall_num)
  {
    case SYS_EXIT:
    {
      //grab the exit status from the stack pointer
      validate_read(f->esp + 4, 4);
      int status = *(int *)(f->esp + 4);
      thread_current()->exitStatus = status;//set the retrieved exit status
      thread_exit();
      break;
    }
    case SYS_WRITE:
    {
      //validate the values that will be pulled
      validate_read(f->esp + 4,  4);
      validate_read(f->esp + 8,  4);
      validate_read(f->esp + 12, 4);

      //read the three arguments of the write(fd, buffer, size) syscall off of the user's stacks
      int fd = *(int *)(f->esp + 4);
      void *buffer = *(void **)(f->esp + 8);
      unsigned size = *(unsigned *)(f->esp + 12);

      // validate_buffer(buffer, size);//validate the buffer as well

      //if the file descriptor (fd) is stdout
      if (fd == 1)
      {
        //set up what needs to be printed
        putbuf(buffer, size);
        f->eax = size;
      }
      else
      {
        f->eax = -1;
      }

      break;
    }

    default:
      thread_current()->exitStatus = -1;
      thread_exit();
      break;
  }
}