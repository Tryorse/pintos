/** This file is derived from source code for the Nachos
   instructional operating system.  The Nachos copyright notice
   is reproduced in full below. */

/** Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
*/

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

static bool thread_priority_comparison(const struct list_elem *first, const struct list_elem *second, void *aux UNUSED);

/** Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   - down or "P": wait for the value to become positive, then
     decrement it.

   - up or "V": increment the value (and wake up one waiting
     thread, if any). */
void sema_init (struct semaphore *sema, unsigned value) 
{
  ASSERT (sema != NULL);

  sema->value = value;
  list_init (&sema->waiters);
}

//take in two list elements that represent position of threads in the list and return a boolean based on their priorities
static bool thread_priority_comparison(const struct list_elem *first, const struct list_elem *second, void *aux UNUSED)
{
  struct thread *first_data = list_entry(first, struct thread, elem);
  struct thread *second_data = list_entry(second, struct thread, elem);

  return first_data->priority > second_data->priority;//use the higher priority thread first
}

/** Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. */
void sema_down(struct semaphore *sema) 
{//this has been updated to work as a priority semaphore
  enum intr_level old_level;

  ASSERT (sema != NULL);
  ASSERT (!intr_context ());//ensures that are not within an interrupt handler

  old_level = intr_disable();//disable interrupts to make this atomic
  while (sema->value == 0)//while the value of the semaphore is 0, that means the resource is unavailable. It will put the thread to sleep
  {
    // list_push_back (&sema->waiters, &thread_current ()->elem);//put list in the semaphore's waiting queue
    list_insert_ordered(&sema->waiters, &thread_current ()->elem, thread_priority_comparison, NULL);//use list_insert_ordered to make it work as a priority semaphore
    thread_block();//changes the thread's state to THREAD_BLOCKED, removes it from the ready queue
  }

  sema->value--;//eventually, another thread will call sema_up which will incremement the semaphore's value. When that happens and the current thread is called, it will then need to decrement the semaphore value
                //this is because the semaphore value determines how many threads can be out at once. So it is important to decrement the semaphore value because the thread will now occupy a semaphore value
  
  intr_set_level(old_level);//restore the interrupt status to the previous state
}

/** Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */
bool sema_try_down (struct semaphore *sema) 
{
  enum intr_level old_level;
  bool success;

  ASSERT (sema != NULL);

  old_level = intr_disable ();
  if (sema->value > 0) 
    {
      sema->value--;
      success = true; 
    }
  else
    success = false;
  intr_set_level (old_level);

  return success;
}

/** Up or "V" operation on a semaphore.  Increments SEMA's value
   and wakes up one thread of those waiting for SEMA, if any.

   This function may be called from an interrupt handler. */
void sema_up(struct semaphore *sema) 
{//this has been updated to work as a priority semaphore
  enum intr_level old_level;

  ASSERT (sema != NULL);

  bool yield_to_CPU = false;

  old_level = intr_disable ();
  if (!list_empty(&sema->waiters)) {
    struct list_elem *first_entry = list_pop_front(&sema->waiters);//grab the first entry in the semaphore's waiting list and remove it from the list
    struct thread *thread = list_entry(first_entry, struct thread, elem);//grab the value indicated by that list element

    thread_unblock(thread);//wake the thread up
    if (thread->priority > thread_current()->priority)
      yield_to_CPU = true;
        // thread_yield();
  }

  //from the old non-priority implementation. Went in the above if statement
  // thread_unblock (list_entry (list_pop_front (&sema->waiters), struct thread, elem));
  
  sema->value++;
  intr_set_level (old_level);

  if (yield_to_CPU)
    thread_yield();
}

static void sema_test_helper (void *sema_);

/** Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
void sema_self_test (void) 
{
  struct semaphore sema[2];
  int i;

  printf ("Testing semaphores...");
  sema_init (&sema[0], 0);
  sema_init (&sema[1], 0);
  thread_create ("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
  for (i = 0; i < 10; i++) 
    {
      sema_up (&sema[0]);
      sema_down (&sema[1]);
    }
  printf ("done.\n");
}

/** Thread function used by sema_self_test(). */
static void sema_test_helper (void *sema_) 
{
  struct semaphore *sema = sema_;
  int i;

  for (i = 0; i < 10; i++) 
    {
      sema_down (&sema[0]);
      sema_up (&sema[1]);
    }
}

/** Initializes LOCK.  A lock can be held by at most a single
   thread at any given time.  Our locks are not "recursive", that
   is, it is an error for the thread currently holding a lock to
   try to acquire that lock.

   A lock is a specialization of a semaphore with an initial
   value of 1.  The difference between a lock and such a
   semaphore is twofold.  First, a semaphore can have a value
   greater than 1, but a lock can only be owned by a single
   thread at a time.  Second, a semaphore does not have an owner,
   meaning that one thread can "down" the semaphore and then
   another one "up" it, but with a lock the same thread must both
   acquire and release it.  When these restrictions prove
   onerous, it's a good sign that a semaphore should be used,
   instead of a lock. */
void lock_init (struct lock *lock)
{
  ASSERT (lock != NULL);

  lock->holder = NULL;
  sema_init (&lock->semaphore, 1);
}

/** Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void lock_acquire (struct lock *lock)
{
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (!lock_held_by_current_thread (lock));

  sema_down (&lock->semaphore);
  lock->holder = thread_current ();
}

/** Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
bool lock_try_acquire (struct lock *lock)
{
  bool success;

  ASSERT (lock != NULL);
  ASSERT (!lock_held_by_current_thread (lock));

  success = sema_try_down (&lock->semaphore);
  if (success)
    lock->holder = thread_current ();
  return success;
}

/** Releases LOCK, which must be owned by the current thread.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
void lock_release (struct lock *lock) 
{
  ASSERT (lock != NULL);
  ASSERT (lock_held_by_current_thread (lock));

  lock->holder = NULL;
  sema_up (&lock->semaphore);
}

/** Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
bool lock_held_by_current_thread (const struct lock *lock) 
{
  ASSERT (lock != NULL);

  return lock->holder == thread_current ();
}

/** One semaphore in a list. */
struct semaphore_elem 
  {
    struct list_elem elem;              /**< List element. */
    struct semaphore semaphore;         /**< This semaphore. */
  };

/** Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
void cond_init (struct condition *cond)
{
  ASSERT (cond != NULL);

  list_init (&cond->waiters);
}

//need this to account for the semaphore list layer of this. Can probably use the priority comparison thread earlier in the code, but just in case maybe not
static bool cond_priority_comparison(const struct list_elem *first, const struct list_elem *second, void *aux UNUSED)
{
  //grab the semaphore_elem values from the sent in list_elems
  struct semaphore_elem *first_data = list_entry(first, struct semaphore_elem, elem);
  struct semaphore_elem *second_data = list_entry(second, struct semaphore_elem, elem);

  //grab the threads from the semaphore_elem values retrieved
  struct thread *first_data_thread = list_entry(list_front(&first_data->semaphore.waiters), struct thread, elem);
  struct thread *second_data_thread = list_entry(list_front(&second_data->semaphore.waiters), struct thread, elem);

  //use the thread with the higher priority
  return first_data_thread->priority > second_data_thread->priority;
}

/** Atomically releases LOCK and waits for COND to be signaled by
   some other piece of code.  After COND is signaled, LOCK is
   reacquired before returning.  LOCK must be held before calling
   this function.

   The monitor implemented by this function is "Mesa" style, not
   "Hoare" style, that is, sending and receiving a signal are not
   an atomic operation.  Thus, typically the caller must recheck
   the condition after the wait completes and, if necessary, wait
   again.

   A given condition variable is associated with only a single
   lock, but one lock may be associated with any number of
   condition variables.  That is, there is a one-to-many mapping
   from locks to condition variables.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void cond_wait(struct condition *cond, struct lock *lock) 
{//NOTE: for project 1 5.2, need to make it use priority scheduling
  struct semaphore_elem waiter;

  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));
  
  sema_init (&waiter.semaphore, 0);
  // list_push_back (&cond->waiters, &waiter.elem);
  list_insert_ordered(&cond->waiters, &waiter.elem, cond_priority_comparison, NULL);//use this to make it work as priority cond
  lock_release (lock);
  sema_down (&waiter.semaphore);
  lock_acquire (lock);
}

/** If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void cond_signal(struct condition *cond, struct lock *lock UNUSED) 
{//NOTE: for project 1 5.2, need to make it use priority scheduling
  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));

  // if (!list_empty (&cond->waiters)) 
  //   sema_up (&list_entry (list_pop_front (&cond->waiters),
  //                         struct semaphore_elem, elem)->semaphore);

  //if there are no threads in the cond's waiting list
  if (!list_empty(&cond->waiters))
  {
    struct list_elem *first_entry = list_pop_front(&cond->waiters);//grab the first entry in the semaphore's waiting list and remove it from the list
    struct semaphore_elem *semaphore_entry = list_entry(first_entry, struct semaphore_elem, elem);//grab the value indicated by that list element

    sema_up(&(semaphore_entry->semaphore));
  }
}

/** Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void cond_broadcast (struct condition *cond, struct lock *lock) 
{
  ASSERT (cond != NULL);
  ASSERT (lock != NULL);

  while (!list_empty (&cond->waiters))
    cond_signal (cond, lock);
}
