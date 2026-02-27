#include "devices/timer.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include "devices/pit.h"
#include "threads/interrupt.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include <list.h>
#include <stdlib.h>
  
/** See [8254] for hardware details of the 8254 timer chip. */

#if TIMER_FREQ < 19
#error 8254 timer requires TIMER_FREQ >= 19
#endif
#if TIMER_FREQ > 1000
#error TIMER_FREQ <= 1000 recommended
#endif

/** Number of timer ticks since OS booted. */
static int64_t ticks;

/** Number of loops per timer tick.
   Initialized by timer_calibrate(). */
static unsigned loops_per_tick;

static intr_handler_func timer_interrupt;
static bool too_many_loops (unsigned loops);
static void busy_wait (int64_t loops);
static void real_time_sleep (int64_t num, int32_t denom);
static void real_time_delay (int64_t num, int32_t denom);
bool lowerSleepTime(const struct list_elem *first, const struct list_elem *second, void *aux UNUSED);
void putThreadOnTimerList(int64_t awakening_timestamp);
void timer_sleep (int64_t ticks);

//the elements will be a custom struct that holds a pointer to the thread to be run and an int that acts as a timestamp for how long to wait. (It will be calculated by adding the ticks to wait to the current timestamp) 
struct list timerSleepList;//the list that will hold the threads that need to sleep for a time. It needs to be ordered with the shortest waits being earlier
int sleepListSize;

/** Sets up the timer to interrupt TIMER_FREQ times per second,
   and registers the corresponding interrupt. */
void
timer_init (void) 
{
  pit_configure_channel (0, 2, TIMER_FREQ);
  intr_register_ext (0x20, timer_interrupt, "8254 Timer");

  list_init(&timerSleepList);
}

/** Calibrates loops_per_tick, used to implement brief delays. */
void
timer_calibrate (void) 
{
  unsigned high_bit, test_bit;

  ASSERT (intr_get_level () == INTR_ON);
  printf ("Calibrating timer...  ");

  /* Approximate loops_per_tick as the largest power-of-two
     still less than one timer tick. */
  loops_per_tick = 1u << 10;
  while (!too_many_loops (loops_per_tick << 1)) 
    {
      loops_per_tick <<= 1;
      ASSERT (loops_per_tick != 0);
    }

  /* Refine the next 8 bits of loops_per_tick. */
  high_bit = loops_per_tick;
  for (test_bit = high_bit >> 1; test_bit != high_bit >> 10; test_bit >>= 1)
    if (!too_many_loops (high_bit | test_bit))
      loops_per_tick |= test_bit;

  printf ("%'"PRIu64" loops/s.\n", (uint64_t) loops_per_tick * TIMER_FREQ);
}

/** Returns the number of timer ticks since the OS booted. */
int64_t
timer_ticks (void) 
{
  enum intr_level old_level = intr_disable ();
  int64_t t = ticks;
  intr_set_level (old_level);
  return t;
}

/** Returns the number of timer ticks elapsed since THEN, which
   should be a value once returned by timer_ticks(). */
int64_t
timer_elapsed (int64_t then) 
{
  return timer_ticks () - then;
}

/** Sleeps for approximately TICKS timer ticks.  Interrupts must
   be turned on. */
void timer_sleep(int64_t ticks)
{
  //THE BUSY WAITING IMPLEMENTATION
  // int64_t start = timer_ticks();//gets the number of ticks that have currently passed to serve as the start time

  // ASSERT (intr_get_level () == INTR_ON);//will throw an error if interrupts are off. It throws an error if off because if the condition is false, then it throws an errors
  // while (timer_elapsed (start) < ticks)//use timer_elapsed() to see how many ticks have passed since the start and loop while it is less than the time to sleep has not elapsed
  //   thread_yield();//yields to the CPU but does not put thread to sleep

  // if (ticks <= 0)//if no ticks left, end the timer
  //   return;

  //LOGIC: since timer_interrupt() triggers TIMER_FREQ times per second (see timer_init()), keep a global list of threads that need to wait for a number of ticks. Calculate how long after the current time to awaken and store that in the queue with the corresponding thread. Then have timer_interrupt() check the queue and awaken those that need to be
  //       the threads should be ordered in the list with the shortest sleep times being earlier

  //NOTE: timer_ticks() disables interrupts during it's process and then reenables them when it is done
  //set the thread's ticks to sleep for to the amount sent in
  int64_t awakening_timestamp = timer_ticks() + ticks - 1;//calculate the timestamp that the tick needs to awaken after. The "-1" part is important

  //NOTE: in the time between calculating the awakening timestamp above and disabling interrupts below, an interrupt could occur. I believe that would be fine because notjhing is happening that needs to be atomic between. Even if it causes the thread to be put on hold any amount of time, the process will still work fine.
  //      i.e. it needs to wait 10 ticks but then an interrupt causes it to be delayed by 20 ticks. This function will still add it to the timer sleep queue and timer interrupt which checks the queue during it's set frequency will notice. So long as the frequency is enough it should be fine

  //disable interrupts and save the interrupt state from before the disabling
  enum intr_level old_level = intr_disable();

  //assign a timestamp to awaken after and then put the thread onto a special waiting queue
  putThreadOnTimerList(awakening_timestamp);
  //NOTE: to add elements and retrieve them from the Pintos list, you need to add a list_elem object to a struct that will hold what you want to store. Meaning that you will have a struct of some name with all your data to store that will also store the list_elem. When you store though, you MUST send in a pointer to the list_elem. You use list_entry() to retrieve what you stored

  //put the thread to sleep
  thread_block();//thread_block() will make it so that it can only be awoken by thread_unblock(). That will be done in thread_unblock()

  //reenable interrupts by restoring the interrupt state from before
  intr_set_level(old_level);
}

bool lowerSleepTime(const struct list_elem *first, const struct list_elem *second, void *aux UNUSED) {
  struct sleepQueueElement *first_data = list_entry(first, struct sleepQueueElement, positionIndex);
  struct sleepQueueElement *second_data = list_entry(second, struct sleepQueueElement, positionIndex);

  //use "<=" to keep order for equal timestamps
  return first_data->timestampToWakeAfter <= second_data->timestampToWakeAfter;
}

//takes in the timestamp to awaken the thread after. Assumes you want to put the current thread to sleep
void putThreadOnTimerList(int64_t awakening_timestamp) {//NOTE: assumes that the timer sleep queue was initialized already (in timer_init() as of 2/25/2026)
  struct sleepQueueElement *newElement = palloc_get_page(0);//malloc(sizeof *element);//create the element. Do like this because when the function ends, the element's data will be removed if it is made on this function's stack
    if (!newElement)
      PANIC("timer_sleep(): failed allocation");
  
  newElement->timestampToWakeAfter = awakening_timestamp;//assign the timestamp
  newElement->sleepingThread = thread_current();//assign the current thread

  //get the first entry in the list
  struct list_elem *entry = list_begin(&timerSleepList);//get the first thread in the queue and remove it from the queue
  // struct sleepQueueElement *element = list_entry(entry, struct sleepQueueElement, positionIndex);//takes in the pointer to the element stored in the queue, what data structure to convert it to, and the name of the list_elem variable within the struct that holds it

  list_insert_ordered(&timerSleepList, &newElement->positionIndex, lowerSleepTime, NULL);
}

/** Sleeps for approximately MS milliseconds.  Interrupts must be
   turned on. */
void
timer_msleep (int64_t ms) 
{
  real_time_sleep (ms, 1000);
}

/** Sleeps for approximately US microseconds.  Interrupts must be
   turned on. */
void
timer_usleep (int64_t us) 
{
  real_time_sleep (us, 1000 * 1000);
}

/** Sleeps for approximately NS nanoseconds.  Interrupts must be
   turned on. */
void
timer_nsleep (int64_t ns) 
{
  real_time_sleep (ns, 1000 * 1000 * 1000);
}

/** Busy-waits for approximately MS milliseconds.  Interrupts need
   not be turned on.

   Busy waiting wastes CPU cycles, and busy waiting with
   interrupts off for the interval between timer ticks or longer
   will cause timer ticks to be lost.  Thus, use timer_msleep()
   instead if interrupts are enabled. */
void
timer_mdelay (int64_t ms) 
{
  real_time_delay (ms, 1000);
}

/** Sleeps for approximately US microseconds.  Interrupts need not
   be turned on.

   Busy waiting wastes CPU cycles, and busy waiting with
   interrupts off for the interval between timer ticks or longer
   will cause timer ticks to be lost.  Thus, use timer_usleep()
   instead if interrupts are enabled. */
void
timer_udelay (int64_t us) 
{
  real_time_delay (us, 1000 * 1000);
}

/** Sleeps execution for approximately NS nanoseconds.  Interrupts
   need not be turned on.

   Busy waiting wastes CPU cycles, and busy waiting with
   interrupts off for the interval between timer ticks or longer
   will cause timer ticks to be lost.  Thus, use timer_nsleep()
   instead if interrupts are enabled.*/
void
timer_ndelay (int64_t ns) 
{
  real_time_delay (ns, 1000 * 1000 * 1000);
}

/** Prints timer statistics. */
void
timer_print_stats (void) 
{
  printf ("Timer: %"PRId64" ticks\n", timer_ticks ());
}

/** Timer interrupt handler. */
static void timer_interrupt (struct intr_frame *args UNUSED)
{
  ticks++;
  thread_tick();

  //below is the process for waking up sleeping timer threads. Since are in an interrupt handler, interrupts are disabled already

  //while there are still threads in the sleep list
  while (!list_empty(&timerSleepList)) {
    //grab the element at the front of the list
    struct list_elem *entry = list_begin(&timerSleepList);//get the first thread in the queue and remove it from the queue
    struct sleepQueueElement *element = list_entry(entry, struct sleepQueueElement, positionIndex);//takes in the pointer to the element stored in the queue, what data structure to convert it to, and the name of the list_elem variable within the struct that holds it

    //if the thread should stay asleep
    if (element->timestampToWakeAfter > ticks)
        break;//end the loop

    //if got here then it should wake up
    list_pop_front(&timerSleepList);//remove from the list
    thread_unblock(element->sleepingThread);//wake up the thread
    palloc_free_page(element);//free up the memory of element because it was created with the sole purpose of storing tracking info for timed sleep. Since it is not being added back in, it is no longer needed. Free it to prevent memory leak
  }
}

/** Returns true if LOOPS iterations waits for more than one timer
   tick, otherwise false. */
static bool
too_many_loops (unsigned loops) 
{
  /* Wait for a timer tick. */
  int64_t start = ticks;
  while (ticks == start)
    barrier ();

  /* Run LOOPS loops. */
  start = ticks;
  busy_wait (loops);

  /* If the tick count changed, we iterated too long. */
  barrier ();
  return start != ticks;
}

/** Iterates through a simple loop LOOPS times, for implementing
   brief delays.

   Marked NO_INLINE because code alignment can significantly
   affect timings, so that if this function was inlined
   differently in different places the results would be difficult
   to predict. */
static void NO_INLINE
busy_wait (int64_t loops) 
{
  while (loops-- > 0)
    barrier ();
}

/** Sleep for approximately NUM/DENOM seconds. */
static void
real_time_sleep (int64_t num, int32_t denom) 
{
  /* Convert NUM/DENOM seconds into timer ticks, rounding down.
          
        (NUM / DENOM) s          
     ---------------------- = NUM * TIMER_FREQ / DENOM ticks. 
     1 s / TIMER_FREQ ticks
  */
  int64_t ticks = num * TIMER_FREQ / denom;

  ASSERT (intr_get_level () == INTR_ON);
  if (ticks > 0)
    {
      /* We're waiting for at least one full timer tick.  Use
         timer_sleep() because it will yield the CPU to other
         processes. */                
      timer_sleep (ticks); 
    }
  else 
    {
      /* Otherwise, use a busy-wait loop for more accurate
         sub-tick timing. */
      real_time_delay (num, denom); 
    }
}

/** Busy-wait for approximately NUM/DENOM seconds. */
static void
real_time_delay (int64_t num, int32_t denom)
{
  /* Scale the numerator and denominator down by 1000 to avoid
     the possibility of overflow. */
  ASSERT (denom % 1000 == 0);
  busy_wait (loops_per_tick * num / 1000 * TIMER_FREQ / (denom / 1000)); 
}
