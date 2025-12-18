#include <stdatomic.h>
#include <stdio.h>

#define THREADPOOL_IMPLEMENTATION
#include "threadpool.h"

#define TRY(fn)                                                                \
	do {                                                                   \
		int err = fn();                                                \
		if (err) {                                                     \
			printf("KO	%s: %d\n", #fn, err);                  \
			return err;                                            \
		} else {                                                       \
			printf("OK	%s\n", #fn);                           \
		}                                                              \
	} while (0)

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
	atomic_int counter = 0;
	struct task t = {0};
	struct tpool_batch b = {0};

	tpool_init(&tpool, (struct tpool_config){.threads_max = 8});

	t.counter = &counter;
	t.inner.work = task_work;
	b = tpool_batch_from_task(&t.inner);

	tpool_schedule(&tpool, b);
	tpool_deinit(&tpool);

	if (counter != 1) {
		printf("expected 1, got %d\n", counter);
		return 1;
	}
	return 0;
}

static int thousand_tasks(void)
{
	struct tpool tpool = {0};
	atomic_int counter = 0;
	struct task tasks[1000];
	struct tpool_batch batch = {0};
	struct tpool_batch b = {0};

	tpool_init(&tpool, (struct tpool_config){.threads_max = 8});

	for (int i = 0; i < 1000; i++) {
		struct task *t = &tasks[i];
		t->counter = &counter;
		t->inner.work = task_work;

		b = tpool_batch_from_task(&t->inner);

		if (i == 0)
			batch = b;
		else
			tpool_batch_push(&batch, b);
	}

	tpool_schedule(&tpool, batch);
	tpool_deinit(&tpool);

	if (counter != 1000) {
		printf("expected 1, got %d\n", counter);
		return 1;
	}
	return 0;
}

int main(void)
{
	printf("executing tests...\n");
	TRY(init_deinit);
	TRY(single_task);
	TRY(thousand_tasks);
	printf("all tests are ok\n");
	return 0;
}
