/*
 * Header file for reservations for dma-buf and ttm
 *
 * Copyright(C) 2011 Linaro Limited. All rights reserved.
 * Copyright (C) 2012-2013 Canonical Ltd
 * Copyright (C) 2012 Texas Instruments
 *
 * Authors:
 * Rob Clark <robdclark@gmail.com>
 * Maarten Lankhorst <maarten.lankhorst@canonical.com>
 * Thomas Hellstrom <thellstrom-at-vmware-dot-com>
 *
 * Based on bo.c which bears the following copyright notice,
 * but is dual licensed:
 *
 * Copyright (c) 2006-2009 VMware, Inc., Palo Alto, CA., USA
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#ifndef _LINUX_RESERVATION_H
#define _LINUX_RESERVATION_H

#include <linux/ww_mutex.h>
#include <linux/dma-fence.h>
#include <linux/slab.h>
#include <linux/seqlock.h>
#include <linux/rcupdate.h>
#ifdef __FreeBSD__
/* On Linux linux/preempt.h is included through linux/seqlock.h */
#include <linux/preempt.h>
#include <sys/param.h>
#include <sys/lock.h>
#include <sys/rwlock.h>
#endif

extern struct ww_class reservation_ww_class;
extern struct lock_class_key reservation_seqcount_class;
extern const char reservation_seqcount_string[];

/**
 * struct dma_resv_list - a list of shared fences
 * @rcu: for internal use
 * @shared_count: table of shared fences
 * @shared_max: for growing shared fence table
 * @shared: shared fence table
 */
struct dma_resv_list {
	struct rcu_head rcu;
	u32 shared_count, shared_max;
	struct dma_fence __rcu *shared[];
};

/**
 * struct dma_resv - a reservation object manages fences for a buffer
 * @lock: update side lock
 * @seq: sequence count for managing RCU read-side synchronization
 * @fence_excl: the exclusive fence, if there is one currently
 * @fence: list of current shared fences
 */
struct dma_resv {
	struct ww_mutex lock;
	seqcount_t seq;
#ifdef __FreeBSD__
	struct rwlock rw;
#endif

	struct dma_fence __rcu *fence_excl;
	struct dma_resv_list __rcu *fence;
};

#define dma_resv_held(obj) lockdep_is_held(&(obj)->lock.base)
#define dma_resv_assert_held(obj) lockdep_assert_held(&(obj)->lock.base)

/**
 * dma_resv_get_list - get the reservation object's
 * shared fence list, with update-side lock held
 * @obj: the reservation object
 *
 * Returns the shared fence list.  Does NOT take references to
 * the fence.  The obj->lock must be held.
 */
static inline struct dma_resv_list *dma_resv_get_list(struct dma_resv *obj)
{
	return rcu_dereference_protected(obj->fence,
					 dma_resv_held(obj));
}

/**
 * dma_resv_lock - lock the reservation object
 * @obj: the reservation object
 * @ctx: the locking context
 *
 * Locks the reservation object for exclusive access and modification. Note,
 * that the lock is only against other writers, readers will run concurrently
 * with a writer under RCU. The seqlock is used to notify readers if they
 * overlap with a writer.
 *
 * As the reservation object may be locked by multiple parties in an
 * undefined order, a #ww_acquire_ctx is passed to unwind if a cycle
 * is detected. See ww_mutex_lock() and ww_acquire_init(). A reservation
 * object may be locked by itself by passing NULL as @ctx.
 */
static inline int dma_resv_lock(struct dma_resv *obj,
				struct ww_acquire_ctx *ctx)
{
	return ww_mutex_lock(&obj->lock, ctx);
}

/**
 * dma_resv_lock_interruptible - lock the reservation object
 * @obj: the reservation object
 * @ctx: the locking context
 *
 * Locks the reservation object interruptible for exclusive access and
 * modification. Note, that the lock is only against other writers, readers
 * will run concurrently with a writer under RCU. The seqlock is used to
 * notify readers if they overlap with a writer.
 *
 * As the reservation object may be locked by multiple parties in an
 * undefined order, a #ww_acquire_ctx is passed to unwind if a cycle
 * is detected. See ww_mutex_lock() and ww_acquire_init(). A reservation
 * object may be locked by itself by passing NULL as @ctx.
 */
static inline int dma_resv_lock_interruptible(struct dma_resv *obj,
					      struct ww_acquire_ctx *ctx)
{
	return ww_mutex_lock_interruptible(&obj->lock, ctx);
}

/**
 * dma_resv_lock_slow - slowpath lock the reservation object
 * @obj: the reservation object
 * @ctx: the locking context
 *
 * Acquires the reservation object after a die case. This function
 * will sleep until the lock becomes available. See dma_resv_lock() as
 * well.
 */
static inline void dma_resv_lock_slow(struct dma_resv *obj,
				      struct ww_acquire_ctx *ctx)
{
	ww_mutex_lock_slow(&obj->lock, ctx);
}

/**
 * dma_resv_lock_slow_interruptible - slowpath lock the reservation
 * object, interruptible
 * @obj: the reservation object
 * @ctx: the locking context
 *
 * Acquires the reservation object interruptible after a die case. This function
 * will sleep until the lock becomes available. See
 * dma_resv_lock_interruptible() as well.
 */
static inline int dma_resv_lock_slow_interruptible(struct dma_resv *obj,
						   struct ww_acquire_ctx *ctx)
{
	return ww_mutex_lock_slow_interruptible(&obj->lock, ctx);
}

/**
 * dma_resv_trylock - trylock the reservation object
 * @obj: the reservation object
 *
 * Tries to lock the reservation object for exclusive access and modification.
 * Note, that the lock is only against other writers, readers will run
 * concurrently with a writer under RCU. The seqlock is used to notify readers
 * if they overlap with a writer.
 *
 * Also note that since no context is provided, no deadlock protection is
 * possible.
 *
 * Returns true if the lock was acquired, false otherwise.
 */
static inline bool __must_check dma_resv_trylock(struct dma_resv *obj)
{
	return ww_mutex_trylock(&obj->lock);
}

/**
 * dma_resv_is_locked - is the reservation object locked
 * @obj: the reservation object
 *
 * Returns true if the mutex is locked, false if unlocked.
 */
static inline bool dma_resv_is_locked(struct dma_resv *obj)
{
	return ww_mutex_is_locked(&obj->lock);
}

/**
 * dma_resv_locking_ctx - returns the context used to lock the object
 * @obj: the reservation object
 *
 * Returns the context used to lock a reservation object or NULL if no context
 * was used or the object is not locked at all.
 */
static inline struct ww_acquire_ctx *dma_resv_locking_ctx(struct dma_resv *obj)
{
	return READ_ONCE(obj->lock.ctx);
}

/**
 * dma_resv_unlock - unlock the reservation object
 * @obj: the reservation object
 *
 * Unlocks the reservation object following exclusive access.
 */
static inline void dma_resv_unlock(struct dma_resv *obj)
{
#ifdef CONFIG_DEBUG_MUTEXES
	/* Test shared fence slot reservation */
	if (rcu_access_pointer(obj->fence)) {
		struct dma_resv_list *fence = dma_resv_get_list(obj);

		fence->shared_max = fence->shared_count;
	}
#endif
	ww_mutex_unlock(&obj->lock);
}

/**
 * dma_resv_get_excl - get the reservation object's
 * exclusive fence, with update-side lock held
 * @obj: the reservation object
 *
 * Returns the exclusive fence (if any).  Does NOT take a
 * reference. Writers must hold obj->lock, readers may only
 * hold a RCU read side lock.
 *
 * RETURNS
 * The exclusive fence or NULL
 */
static inline struct dma_fence *
dma_resv_get_excl(struct dma_resv *obj)
{
	return rcu_dereference_protected(obj->fence_excl,
					 dma_resv_held(obj));
}

/**
 * dma_resv_get_excl_rcu - get the reservation object's
 * exclusive fence, without lock held.
 * @obj: the reservation object
 *
 * If there is an exclusive fence, this atomically increments it's
 * reference count and returns it.
 *
 * RETURNS
 * The exclusive fence or NULL if none
 */
static inline struct dma_fence *
dma_resv_get_excl_rcu(struct dma_resv *obj)
{
	struct dma_fence *fence;

	if (!rcu_access_pointer(obj->fence_excl))
		return NULL;

	rcu_read_lock();
	fence = dma_fence_get_rcu_safe(&obj->fence_excl);
	rcu_read_unlock();

	return fence;
}

void dma_resv_init(struct dma_resv *obj);
void dma_resv_fini(struct dma_resv *obj);
int dma_resv_reserve_shared(struct dma_resv *obj, unsigned int num_fences);
void dma_resv_add_shared_fence(struct dma_resv *obj, struct dma_fence *fence);

void dma_resv_add_excl_fence(struct dma_resv *obj, struct dma_fence *fence);

int dma_resv_get_fences_rcu(struct dma_resv *obj,
			    struct dma_fence **pfence_excl,
			    unsigned *pshared_count,
			    struct dma_fence ***pshared);

int dma_resv_copy_fences(struct dma_resv *dst, struct dma_resv *src);

long dma_resv_wait_timeout_rcu(struct dma_resv *obj, bool wait_all, bool intr,
			       unsigned long timeout);

bool dma_resv_test_signaled_rcu(struct dma_resv *obj, bool test_all);

#endif /* _LINUX_RESERVATION_H */