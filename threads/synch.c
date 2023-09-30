/* This file is derived from source code for the Nachos
   instructional operating system.  The Nachos copyright notice
   is reproduced in full below. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
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

/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   - down or "P": wait for the value to become positive, then
   decrement it.

   - up or "V": increment the value (and wake up one waiting
   thread, if any). */
void
sema_init (struct semaphore *sema, unsigned value) {
	ASSERT (sema != NULL);

	sema->value = value;
	list_init (&sema->waiters);
}

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. This is
   sema_down function. */
void sema_down (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);
	ASSERT (!intr_context ());

	old_level = intr_disable ();
	while (sema -> value == 0) {
		list_insert_ordered(&sema -> waiters, &thread_current () -> elem, order_by_priority, NULL);
		thread_block ();
	}
	sema->value--;
	intr_set_level (old_level);
}

/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */
bool
sema_try_down (struct semaphore *sema) {
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

/* Up or "V" operation on a semaphore.  Increments SEMA's value
   and wakes up one thread of those waiting for SEMA, if any.

   This function may be called from an interrupt handler. */
void sema_up (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);

	old_level = intr_disable ();
	if (!list_empty (&sema->waiters)){
		// Problem) sort 추가로 priority-donate-sema 해결: set_priority에서 변경되면서 깨지는거 같음
		list_sort(&sema->waiters, order_by_priority, NULL);
		thread_unblock (list_entry (list_pop_front(&sema->waiters), struct thread, elem));
	}
	sema->value++;
	cmp_cur_and_ready();
	intr_set_level (old_level);
}

static void sema_test_helper (void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
void
sema_self_test (void) {
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

/* Thread function used by sema_self_test(). */
static void
sema_test_helper (void *sema_) {
	struct semaphore *sema = sema_;
	int i;

	for (i = 0; i < 10; i++)
	{
		sema_down (&sema[0]);
		sema_up (&sema[1]);
	}
}

/* Initializes LOCK.  A lock can be held by at most a single
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
void
lock_init (struct lock *lock) {
	ASSERT (lock != NULL);

	lock->holder = NULL;
	sema_init (&lock->semaphore, 1);
}

/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void lock_acquire (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (!lock_held_by_current_thread (lock));

	struct thread *cur_t = thread_current();

	if(lock -> holder){
		cur_t -> wait_on_lock = lock;

		if(lock -> holder -> priority < cur_t -> priority){
			list_insert_ordered(&lock -> holder -> donations, &cur_t -> delem, order_by_priority_delem, NULL);
			donate_priority(cur_t);
		}
	}

	sema_down (&lock->semaphore);
	cur_t -> wait_on_lock = NULL;
	lock -> holder = cur_t;
}

/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
bool
lock_try_acquire (struct lock *lock) {
	bool success;

	ASSERT (lock != NULL);
	ASSERT (!lock_held_by_current_thread (lock));

	success = sema_try_down (&lock->semaphore);
	if (success)
		lock->holder = thread_current ();
	return success;
}

/* Releases LOCK, which must be owned by the current thread.
   This is lock_release function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
void lock_release (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (lock_held_by_current_thread (lock));

	// 1. lock holder 다음 대기자의 우선순위를 올려줘야함 (o) line:248
	// 2. lock holder 의 우선순위는 원래대로 복구 해줘야함 (o) line:271
	// 3. lock holder가 가진 donations 제거해줘야함 (o) line:256
	// 4. donation list에 있는 thread의 wait_on_lock과 현재 release하는 lock이 같다면 (x)

	// holder가 release 해주는애임

	// if(!list_empty(&lock -> semaphore.waiters)){
 	// 	struct thread *next_waiter = get_thread(list_begin(&lock -> semaphore.waiters));

	// 	// waiters의 첫 요소의 priority 확인해서 release 하는 thread보다 작으면
	// 	if(next_waiter -> priority < lock -> holder -> priority){ 
	// 		next_waiter -> priority = lock -> holder -> priority;
	// 	}
	// }

	struct thread *cur_t = lock -> holder;
	struct list *donations = &cur_t -> donations;
	struct thread *t;

	if(!list_empty(donations)){
		struct list_elem *e;

		for (e = list_begin(donations); e != list_end(donations); e = list_next(e)){
			t = get_thread_delem(e);
			
			if (t -> wait_on_lock == lock){
			    list_remove(&t -> delem);
				// Problem) priority-donate-one 해결됨 donations에 여러개 있나????
				// 정상 흐름) main -> acq2 -> acq1
				// break 흐름) main -> acq2 -> main -> acq1
				// 해당 lock에서 받은 donations 다 제거 안해서 ready_list에서 donate 받은 우선순위로 점유중이였음
				//break; 
			}
		}

		// struct thread *cur_t = get_thread_delem(list_begin(donations));
		// struct thread *last_t = get_thread_delem(list_end(donations));

		// while(last_t != cur_t){
		// 	if(lock == cur_t -> wait_on_lock){
		// 		list_remove(&cur_t -> delem);
		// 		break;
		// 	}

		// 	cur_t = get_thread_delem(list_next(&cur_t -> delem));
		// }
	}

	// release할 thread orginal priority로 복귀
	// problem) priority-donate-one 해결되는 코드
	update_priority(cur_t);
	
	lock -> holder = NULL;
	sema_up (&lock->semaphore);
}

void update_priority(struct thread *cur_t){
	struct list *donations = &cur_t -> donations;

	cur_t -> priority = cur_t -> org_priority;

	if(!list_empty(donations)) {
		// set_priority 때문에 sort 해줘야 할듯 중간에 값 바꿔버리면 엉키는데
		list_sort(donations, order_by_priority_delem, NULL);

		struct thread *front = get_thread_delem(list_begin(donations));

		if (front -> priority > cur_t -> priority){
			cur_t -> priority = front -> priority;
		}
	}
}

bool order_by_priority_delem(const struct list_elem *a, const struct list_elem *b, void *aux) {
    return get_thread_delem(a)->priority > get_thread_delem(b)->priority;
}

struct thread *get_thread_delem(struct list_elem *e){
	return list_entry(e, struct thread, delem);
}

/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
bool
lock_held_by_current_thread (const struct lock *lock) {
	ASSERT (lock != NULL);

	return lock->holder == thread_current ();
}

/* One semaphore in a list. */
struct semaphore_elem {
	struct list_elem elem;              /* List element. */
	struct semaphore semaphore;         /* This semaphore. */
};

/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
void
cond_init (struct condition *cond) {
	ASSERT (cond != NULL);

	list_init (&cond->waiters);
}

/* Atomically releases LOCK and waits for COND to be signaled by
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
void cond_wait (struct condition *cond, struct lock *lock) {
	struct semaphore_elem waiter;

	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	sema_init (&waiter.semaphore, 0);
	list_push_back (&cond->waiters, &waiter.elem);
	lock_release (lock);
	sema_down (&waiter.semaphore);
	lock_acquire (lock);
}

/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void cond_signal (struct condition *cond, struct lock *lock UNUSED) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	if (!list_empty (&cond->waiters)){
		//list_sort(&cond->waiters, cmp_sema_priority, NULL);
		sema_up (&list_entry (list_pop_front (&cond->waiters),
					struct semaphore_elem, elem)->semaphore);
	}
}

/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void cond_broadcast (struct condition *cond, struct lock *lock) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);

	while (!list_empty (&cond->waiters))
		cond_signal (cond, lock);
}

void donate_priority(struct thread *cur_t) {
	int donor_priority = cur_t -> priority;
	struct thread* holder;

	while(cur_t -> wait_on_lock){
		holder = cur_t -> wait_on_lock -> holder;
		holder -> priority = donor_priority;
		cur_t = holder;
	}
}

void print_current(){
	struct thread *t = thread_current();

	printf("Name = %s, Donated Priority = %d, Original Priority = %d Is_Lock? = %s\n"
	, t -> name
	, t -> priority
	, t -> org_priority
	, t -> wait_on_lock == NULL ? "lock" : "unlock"
	);
}