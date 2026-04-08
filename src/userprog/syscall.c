#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "devices/shutdown.h"

static void syscall_handler (struct intr_frame *);
static void validate_pointer (const void *user_address);
static void validate_buffer (void *buffer, unsigned size);
static void validate_read(const void *addr, size_t size);
static uint32_t copy_in_u32(const void *address);
int syscall_write(int fd, const void *buffer, unsigned size);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f) 
{
  //validate the stack pointer that is to be grabbed
  validate_read(f->esp, 4);
  int syscall_num = *(int *)(f->esp);

  switch (syscall_num)
  {
    case (SYS_EXIT):
    {
      //grab the exit status from the stack pointer
      validate_read(f->esp + 4, 4);

      //grab the exit status from the stack pointer
      int status = *(int *)(f->esp + 4);

      thread_current()->exitStatus = status;//set the retrieved exit status
      thread_exit();
      
      break;
    }
    case (SYS_WRITE):
    {
      //read the three arguments of the write(fd, buffer, size) syscall off of the user's stacks
      int fd = (int)copy_in_u32(f->esp + 4);
      void *buffer = (const void *)copy_in_u32(f->esp + 8);
      unsigned size = (unsigned)copy_in_u32(f->esp + 12);

      //make sure the buffer is valid
      validate_buffer(buffer, size);
      f->eax = syscall_write(fd, buffer, size);//eax is the register field

      break;
    }
    case (SYS_HALT):
    {//the instructions state that halt() should shut down the system
      shutdown_power_off();//make it do so
      break;
    }
    default:
      thread_current()->exitStatus = -1;
      thread_exit();
      break;
  }
}

static void validate_buffer(void *buffer, unsigned size)
{
  if (size == 0)
  {
    return;
  }

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
static void validate_pointer(const void *user_address)
{
  //if no user address or it is not a user virtual address or the page directory is null
  if (user_address == NULL || !is_user_vaddr(user_address) || pagedir_get_page(thread_current()->pagedir, user_address) == NULL)
  {//it is a failure
    thread_current()->exitStatus = -1;//set exit status to -1
    thread_exit();//end the process (thread)
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

static uint32_t copy_in_u32(const void *address)
{
  //validate the address
  validate_read(address, 4);
  return *(uint32_t *)address;
}

//provided by the project 2 instructions
int syscall_write(int fd, const void *buffer, unsigned size)
{
  //for simplicity, only handle writing to stdout (fd = 1)
  if (fd != 1)
  {
    return -1;
  }

  putbuf(buffer, size);
  
  return size;
}