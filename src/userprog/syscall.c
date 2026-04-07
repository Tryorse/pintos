#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

static void syscall_handler (struct intr_frame *);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f) 
{
  int syscall_num = *(int *)(f->esp);

  switch (syscall_num)
  {
    case SYS_EXIT:
    {
      //grab the exit status from the stack pointer
      int status = *(int *)(f->esp + 4);
      thread_current()->exitStatus = status;//set the retrieved exit status
      thread_exit();
      break;
    }
    case SYS_WRITE:
    {
      //read the three arguments of the write(fd, buffer, size) syscall off of the user's stacks
      int fd = *(int *)(f->esp + 4);
      void *buffer = *(void **)(f->esp + 8);
      unsigned size = *(unsigned *)(f->esp + 12);

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