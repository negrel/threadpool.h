#include <stdatomic.h>
#include <stdio.h>

#define THREADPOOL_IMPLEMENTATION
#include "threadpool.h"

#define TRY(fn)                                                                \
	do {                                                                   \
		int err = fn();                                                \
		if (err) {                                                     \
			printf("%s return non zero exit code: %d\n", #fn,      \
			       err);                                           \
			return err;                                            \
		}                                                              \
	} while (0);

static int init_deinit(void)
{
	struct tpool tpool = {0};
	tpool_init(&tpool, (struct tpool_config){.threads_max = 8});
	tpool_deinit(&tpool);

	return 0;
}

struct task {
	struct tpool_task inner;
	atomic_int *counter;
};

static void task_work(struct tpool_task *tt)
{
	struct task *t = (void *)tt;
	atomic_fetch_add(t->counter, 1);
}

static int single_task(void)
{
	struct tpool tpool = {0};
	atomic_int counter;
	struct task t = {0};
	struct tpool_batch b = {0};

	tpool_init(&tpool, (struct tpool_config){.threads_max = 8});

	t.counter = &counter;
	t.inner.work = task_work;
	b = tpool_batch_from_task(&t.inner);

	tpool_schedule(&tpool, b);
	tpool_deinit(&tpool);

	return atomic_load(&counter) == 1;
}

int main(void)
{
	TRY(init_deinit);
	TRY(single_task);
	return 0;
}
