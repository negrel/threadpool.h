/**
 * Allocation free (excluding spawning thread) thread pool with batch
 * scheduling. Threads are lazily spawned.
 *
 * MIT License
 *
 * Copyright (c) 2024 Alexandre Negrel
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef THREADPOOL_H_INCLUDE
#define THREADPOOL_H_INCLUDE

#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdbool.h>

#ifndef TPOOL_DEFAULT_STACK_SIZE
#define TPOOL_DEFAULT_STACK_SIZE (16 * 1024 * 1024)
#endif /* TPOOL_DEFAULT_STACK_SIZE */

#ifndef TPOOL_DEFAULT_THREADS_MAX
#define TPOOL_DEFAULT_THREADS_MAX 16
#endif /* TPOOL_DEFAULT_THREADS_MAX */

struct tpool_task;

typedef void (*tpool_work_fn)(struct tpool_task *task);

/*
 * A task represents the unit of work / job / execution that the thread pool
 * schedules. The user provides a `work` function which is invoked when the task
 * can run on a thread.
 */
struct tpool_task {
	struct tpool_task *next;
	tpool_work_fn work;
};

/* An unordered collection of tasks which can be submitted for scheduling as a
 * group.
 */
struct tpool_batch {
	unsigned int size;
	struct tpool_task *head;
	struct tpool_task *tail;
};

/**
 * Create a batch containing a single task.
 */
struct tpool_batch tpool_batch_from_task(struct tpool_task *t);

/**
 * Push another batch into this batch, taking ownership of its tasks.
 */
void tpool_batch_push(struct tpool_batch *b, struct tpool_batch o);

/**
 * A thread part of the pool. This is a private structure, use at your own risk.
 */
struct tpool_thread {
	struct tpool_thread *next;
	pthread_t tid;
};

/**
 * Thread pool configuration options.
 */
struct tpool_config {
	size_t stack_size;
	unsigned int threads_max;
};

/**
 * Thread pool.
 */
struct tpool {
	struct tpool_config cfg;
	atomic_uint threads_count;
	atomic_uint threads_idle;

	// Mutex protected fields.
	pthread_mutex_t mu;
	struct tpool_batch work_queue;
	bool done;
	pthread_cond_t cond;
};

/**
 * Initializes a thread pool. This function doesn't spawn any thread.
 */
void tpool_init(struct tpool *t, struct tpool_config cfg);

/**
 * Deinitializes a thread pool and clean up all threads. This function blocks
 * until all executing tasks are done.
 */
void tpool_deinit(struct tpool *t);

/**
 * Schedules a batch of task on the thread pool. If there is no idle thread and
 * thread limit isn't reached a new thread is spawned. In case of failure,
 * negative error code of pthread_create() is returned.
 */
int tpool_schedule(struct tpool *t, struct tpool_batch b);

#ifdef THREADPOOL_IMPLEMENTATION

struct tpool_batch tpool_batch_from_task(struct tpool_task *t)
{
	struct tpool_batch b;
	b.size = 1;
	b.head = t;
	b.tail = t;
	return b;
}

void tpool_batch_push(struct tpool_batch *b, struct tpool_batch o)
{
	if (o.size == 0)
		return;
	if (b->size == 0)
		*b = o;
	else {
		b->tail->next = o.head;
		b->tail = o.tail;
		b->size += o.size;
	}
}

static struct tpool_task *tpool_batch_pop(struct tpool_batch *b)
{
	if (b->size == 0)
		return NULL;
	struct tpool_task *t = b->head;
	b->head = b->head->next;
	if (b->tail == t)
		b->tail = b->head = NULL;
	b->size--;
	return t;
}

void tpool_init(struct tpool *t, struct tpool_config cfg)
{
	cfg.threads_max =
	    cfg.threads_max == 0 ? TPOOL_DEFAULT_THREADS_MAX : cfg.threads_max;
	cfg.stack_size =
	    cfg.stack_size == 0 ? TPOOL_DEFAULT_STACK_SIZE : cfg.stack_size;
	t->cfg = cfg;
	t->threads_count = 0;
	t->threads_idle = 0;
	pthread_mutex_init(&t->mu, NULL);
	t->work_queue = (struct tpool_batch){0};
	t->done = false;
	pthread_cond_init(&t->cond, NULL);
}

void tpool_deinit(struct tpool *t)
{
	pthread_mutex_lock(&t->mu);
	t->done = true;
	pthread_mutex_unlock(&t->mu);

	pthread_cond_broadcast(&t->cond);

	while (atomic_load(&t->threads_count) > 0)
		sched_yield();

	pthread_cond_destroy(&t->cond);
}

/**
 * Main function of thread part of the thread pool.
 */
static void *tpool_thread_main(void *ptr)
{
	struct tpool *t = ptr;

	while (1) {
		pthread_mutex_lock(&t->mu);
		if (t->work_queue.size == 0) {
			if (t->done) {
				atomic_fetch_sub(&t->threads_count, 1);
				pthread_mutex_unlock(&t->mu);
				return NULL;
			}
			atomic_fetch_add(&t->threads_idle, 1);
			pthread_cond_wait(&t->cond, &t->mu);
			atomic_fetch_sub(&t->threads_idle, 1);
		}

		struct tpool_task *task = tpool_batch_pop(&t->work_queue);
		pthread_mutex_unlock(&t->mu);

		if (task != NULL)
			(*task->work)(task);
	}
}

int tpool_schedule(struct tpool *t, struct tpool_batch b)
{
	pthread_attr_t attr;
	pthread_t thread;
	int err;

	if (b.size == 0)
		return 0;

	// Push work.
	pthread_mutex_lock(&t->mu);
	tpool_batch_push(&t->work_queue, b);
	pthread_mutex_unlock(&t->mu);

	// Spawn new thread if possible and needed.
	unsigned int idle = atomic_load(&t->threads_idle);
	if (idle == 0 && atomic_load(&t->threads_count) < t->cfg.threads_max) {
		err = pthread_attr_init(&attr);
		if (err)
			goto attr_error;

		pthread_attr_setstacksize(&attr, t->cfg.stack_size);

		err = pthread_create(&thread, &attr, &tpool_thread_main, t);
		if (err)
			goto create_error;

		pthread_detach(thread);

		pthread_attr_destroy(&attr);

		atomic_fetch_add(&t->threads_count, 1);
		idle++;
	}

	// Wake up idle thread.
	if (idle > 0) {
		pthread_cond_signal(&t->cond);
	}

	return 0;

create_error:
	pthread_attr_destroy(&attr);
attr_error:
	return -err;
}

#endif /* THREADPOOL_IMPLEMENTATION */

#endif /* THREADPOOL_H_INCLUDE */
