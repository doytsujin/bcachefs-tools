#ifdef CONFIG_BCACHEFS_TESTS

#include "bcachefs.h"
#include "btree_update.h"
#include "journal_reclaim.h"
#include "tests.h"

#include "linux/kthread.h"
#include "linux/random.h"

static void delete_test_keys(struct bch_fs *c)
{
	int ret;

	ret = bch2_btree_delete_range(c, BTREE_ID_EXTENTS,
				      POS(0, 0), POS(0, U64_MAX),
				      ZERO_VERSION, NULL, NULL, NULL);
	BUG_ON(ret);

	ret = bch2_btree_delete_range(c, BTREE_ID_DIRENTS,
				      POS(0, 0), POS(0, U64_MAX),
				      ZERO_VERSION, NULL, NULL, NULL);
	BUG_ON(ret);
}

/* unit tests */

static void test_delete(struct bch_fs *c, u64 nr)
{
	struct btree_iter iter;
	struct bkey_i_cookie k;
	int ret;

	bkey_cookie_init(&k.k_i);

	bch2_btree_iter_init(&iter, c, BTREE_ID_DIRENTS, k.k.p,
			     BTREE_ITER_INTENT);

	ret = bch2_btree_iter_traverse(&iter);
	BUG_ON(ret);

	ret = bch2_btree_insert_at(c, NULL, NULL, NULL, 0,
				   BTREE_INSERT_ENTRY(&iter, &k.k_i));
	BUG_ON(ret);

	pr_info("deleting once");
	ret = bch2_btree_delete_at(&iter, 0);
	BUG_ON(ret);

	pr_info("deleting twice");
	ret = bch2_btree_delete_at(&iter, 0);
	BUG_ON(ret);

	bch2_btree_iter_unlock(&iter);
}

static void test_delete_written(struct bch_fs *c, u64 nr)
{
	struct btree_iter iter;
	struct bkey_i_cookie k;
	int ret;

	bkey_cookie_init(&k.k_i);

	bch2_btree_iter_init(&iter, c, BTREE_ID_DIRENTS, k.k.p,
			     BTREE_ITER_INTENT);

	ret = bch2_btree_iter_traverse(&iter);
	BUG_ON(ret);

	ret = bch2_btree_insert_at(c, NULL, NULL, NULL, 0,
				   BTREE_INSERT_ENTRY(&iter, &k.k_i));
	BUG_ON(ret);

	bch2_journal_flush_all_pins(&c->journal);

	ret = bch2_btree_delete_at(&iter, 0);
	BUG_ON(ret);

	bch2_btree_iter_unlock(&iter);
}

static void test_iterate(struct bch_fs *c, u64 nr)
{
	struct btree_iter iter;
	struct bkey_s_c k;
	u64 i;
	int ret;

	delete_test_keys(c);

	pr_info("inserting test keys");

	for (i = 0; i < nr; i++) {
		struct bkey_i_cookie k;

		bkey_cookie_init(&k.k_i);
		k.k.p.offset = i;

		ret = bch2_btree_insert(c, BTREE_ID_DIRENTS, &k.k_i,
					NULL, NULL, NULL, 0);
		BUG_ON(ret);
	}

	pr_info("iterating forwards");

	i = 0;

	for_each_btree_key(&iter, c, BTREE_ID_DIRENTS, POS(0, 0), 0, k)
		BUG_ON(k.k->p.offset != i++);
	bch2_btree_iter_unlock(&iter);

	BUG_ON(i != nr);

	pr_info("iterating backwards");

	while (!IS_ERR_OR_NULL((k = bch2_btree_iter_prev(&iter)).k))
		BUG_ON(k.k->p.offset != --i);
	bch2_btree_iter_unlock(&iter);

	BUG_ON(i);
}

static void test_iterate_extents(struct bch_fs *c, u64 nr)
{
	struct btree_iter iter;
	struct bkey_s_c k;
	u64 i;
	int ret;

	delete_test_keys(c);

	pr_info("inserting test extents");

	for (i = 0; i < nr; i += 8) {
		struct bkey_i_cookie k;

		bkey_cookie_init(&k.k_i);
		k.k.p.offset = i + 8;
		k.k.size = 8;

		ret = bch2_btree_insert(c, BTREE_ID_EXTENTS, &k.k_i,
					NULL, NULL, NULL, 0);
		BUG_ON(ret);
	}

	pr_info("iterating forwards");

	i = 0;

	for_each_btree_key(&iter, c, BTREE_ID_EXTENTS, POS(0, 0), 0, k) {
		BUG_ON(bkey_start_offset(k.k) != i);
		i = k.k->p.offset;
	}
	bch2_btree_iter_unlock(&iter);

	BUG_ON(i != nr);

	pr_info("iterating backwards");

	while (!IS_ERR_OR_NULL((k = bch2_btree_iter_prev(&iter)).k)) {
		BUG_ON(k.k->p.offset != i);
		i = bkey_start_offset(k.k);
	}
	bch2_btree_iter_unlock(&iter);

	BUG_ON(i);
}

static void test_iterate_slots(struct bch_fs *c, u64 nr)
{
	struct btree_iter iter;
	struct bkey_s_c k;
	u64 i;
	int ret;

	delete_test_keys(c);

	pr_info("inserting test keys");

	for (i = 0; i < nr; i++) {
		struct bkey_i_cookie k;

		bkey_cookie_init(&k.k_i);
		k.k.p.offset = i * 2;

		ret = bch2_btree_insert(c, BTREE_ID_DIRENTS, &k.k_i,
					NULL, NULL, NULL, 0);
		BUG_ON(ret);
	}

	pr_info("iterating forwards");

	i = 0;

	for_each_btree_key(&iter, c, BTREE_ID_DIRENTS, POS(0, 0), 0, k) {
		BUG_ON(k.k->p.offset != i);
		i += 2;
	}
	bch2_btree_iter_unlock(&iter);

	BUG_ON(i != nr * 2);

	pr_info("iterating forwards by slots");

	i = 0;

	for_each_btree_key(&iter, c, BTREE_ID_DIRENTS, POS(0, 0),
			   BTREE_ITER_SLOTS, k) {
		BUG_ON(bkey_deleted(k.k) != (i & 1));
		BUG_ON(k.k->p.offset != i++);

		if (i == nr * 2)
			break;
	}
	bch2_btree_iter_unlock(&iter);
}

static void test_iterate_slots_extents(struct bch_fs *c, u64 nr)
{
	struct btree_iter iter;
	struct bkey_s_c k;
	u64 i;
	int ret;

	delete_test_keys(c);

	pr_info("inserting test keys");

	for (i = 0; i < nr; i += 16) {
		struct bkey_i_cookie k;

		bkey_cookie_init(&k.k_i);
		k.k.p.offset = i + 16;
		k.k.size = 8;

		ret = bch2_btree_insert(c, BTREE_ID_EXTENTS, &k.k_i,
					NULL, NULL, NULL, 0);
		BUG_ON(ret);
	}

	pr_info("iterating forwards");

	i = 0;

	for_each_btree_key(&iter, c, BTREE_ID_EXTENTS, POS(0, 0), 0, k) {
		BUG_ON(bkey_start_offset(k.k) != i + 8);
		BUG_ON(k.k->size != 8);
		i += 16;
	}
	bch2_btree_iter_unlock(&iter);

	BUG_ON(i != nr);

	pr_info("iterating forwards by slots");

	i = 0;

	for_each_btree_key(&iter, c, BTREE_ID_EXTENTS, POS(0, 0),
			   BTREE_ITER_SLOTS, k) {
		BUG_ON(bkey_deleted(k.k) != !(i % 16));

		BUG_ON(bkey_start_offset(k.k) != i);
		BUG_ON(k.k->size != 8);
		i = k.k->p.offset;

		if (i == nr)
			break;
	}
	bch2_btree_iter_unlock(&iter);
}

/* perf tests */

static u64 test_rand(void)
{
	u64 v;
#if 0
	v = prandom_u32();
#else
	prandom_bytes(&v, sizeof(v));
#endif
	return v;
}

static void rand_insert(struct bch_fs *c, u64 nr)
{
	struct bkey_i_cookie k;
	int ret;
	u64 i;

	for (i = 0; i < nr; i++) {
		bkey_cookie_init(&k.k_i);
		k.k.p.offset = test_rand();

		ret = bch2_btree_insert(c, BTREE_ID_DIRENTS, &k.k_i,
					NULL, NULL, NULL, 0);
		BUG_ON(ret);
	}
}

static void rand_lookup(struct bch_fs *c, u64 nr)
{
	u64 i;

	for (i = 0; i < nr; i++) {
		struct btree_iter iter;
		struct bkey_s_c k;

		bch2_btree_iter_init(&iter, c, BTREE_ID_DIRENTS,
				     POS(0, test_rand()), 0);

		k = bch2_btree_iter_peek(&iter);
		bch2_btree_iter_unlock(&iter);
	}
}

static void rand_mixed(struct bch_fs *c, u64 nr)
{
	int ret;
	u64 i;

	for (i = 0; i < nr; i++) {
		struct btree_iter iter;
		struct bkey_s_c k;

		bch2_btree_iter_init(&iter, c, BTREE_ID_DIRENTS,
				     POS(0, test_rand()), 0);

		k = bch2_btree_iter_peek(&iter);

		if (!(i & 3) && k.k) {
			struct bkey_i_cookie k;

			bkey_cookie_init(&k.k_i);
			k.k.p = iter.pos;

			ret = bch2_btree_insert_at(c, NULL, NULL, NULL, 0,
						   BTREE_INSERT_ENTRY(&iter, &k.k_i));
			BUG_ON(ret);
		}

		bch2_btree_iter_unlock(&iter);
	}

}

static void rand_delete(struct bch_fs *c, u64 nr)
{
	struct bkey_i k;
	int ret;
	u64 i;

	for (i = 0; i < nr; i++) {
		bkey_init(&k.k);
		k.k.p.offset = test_rand();

		ret = bch2_btree_insert(c, BTREE_ID_DIRENTS, &k,
					NULL, NULL, NULL, 0);
		BUG_ON(ret);
	}
}

static void seq_insert(struct bch_fs *c, u64 nr)
{
	struct btree_iter iter;
	struct bkey_s_c k;
	struct bkey_i_cookie insert;
	int ret;
	u64 i = 0;

	bkey_cookie_init(&insert.k_i);

	for_each_btree_key(&iter, c, BTREE_ID_DIRENTS, POS_MIN,
			   BTREE_ITER_SLOTS|BTREE_ITER_INTENT, k) {
		insert.k.p = iter.pos;

		ret = bch2_btree_insert_at(c, NULL, NULL, NULL, 0,
				BTREE_INSERT_ENTRY(&iter, &insert.k_i));
		BUG_ON(ret);

		if (++i == nr)
			break;
	}
	bch2_btree_iter_unlock(&iter);
}

static void seq_lookup(struct bch_fs *c, u64 nr)
{
	struct btree_iter iter;
	struct bkey_s_c k;

	for_each_btree_key(&iter, c, BTREE_ID_DIRENTS, POS_MIN, 0, k)
		;
	bch2_btree_iter_unlock(&iter);
}

static void seq_overwrite(struct bch_fs *c, u64 nr)
{
	struct btree_iter iter;
	struct bkey_s_c k;
	int ret;

	for_each_btree_key(&iter, c, BTREE_ID_DIRENTS, POS_MIN,
			   BTREE_ITER_INTENT, k) {
		struct bkey_i_cookie u;

		bkey_reassemble(&u.k_i, k);

		ret = bch2_btree_insert_at(c, NULL, NULL, NULL, 0,
					   BTREE_INSERT_ENTRY(&iter, &u.k_i));
		BUG_ON(ret);
	}
	bch2_btree_iter_unlock(&iter);
}

static void seq_delete(struct bch_fs *c, u64 nr)
{
	int ret;

	ret = bch2_btree_delete_range(c, BTREE_ID_DIRENTS,
				      POS(0, 0), POS(0, U64_MAX),
				      ZERO_VERSION, NULL, NULL, NULL);
	BUG_ON(ret);
}

typedef void (*perf_test_fn)(struct bch_fs *, u64);

struct test_job {
	struct bch_fs			*c;
	u64				nr;
	unsigned			nr_threads;
	perf_test_fn			fn;

	atomic_t			ready;
	wait_queue_head_t		ready_wait;

	atomic_t			done;
	struct completion		done_completion;

	u64				start;
	u64				finish;
};

static int btree_perf_test_thread(void *data)
{
	struct test_job *j = data;

	if (atomic_dec_and_test(&j->ready)) {
		wake_up(&j->ready_wait);
		j->start = sched_clock();
	} else {
		wait_event(j->ready_wait, !atomic_read(&j->ready));
	}

	j->fn(j->c, j->nr / j->nr_threads);

	if (atomic_dec_and_test(&j->done)) {
		j->finish = sched_clock();
		complete(&j->done_completion);
	}

	return 0;
}

void bch2_btree_perf_test(struct bch_fs *c, const char *testname,
			  u64 nr, unsigned nr_threads)
{
	struct test_job j = { .c = c, .nr = nr, .nr_threads = nr_threads };
	char name_buf[20], nr_buf[20], per_sec_buf[20];
	unsigned i;
	u64 time;

	atomic_set(&j.ready, nr_threads);
	init_waitqueue_head(&j.ready_wait);

	atomic_set(&j.done, nr_threads);
	init_completion(&j.done_completion);

#define perf_test(_test)				\
	if (!strcmp(testname, #_test)) j.fn = _test

	perf_test(rand_insert);
	perf_test(rand_lookup);
	perf_test(rand_mixed);
	perf_test(rand_delete);

	perf_test(seq_insert);
	perf_test(seq_lookup);
	perf_test(seq_overwrite);
	perf_test(seq_delete);

	/* a unit test, not a perf test: */
	perf_test(test_delete);
	perf_test(test_delete_written);
	perf_test(test_iterate);
	perf_test(test_iterate_extents);
	perf_test(test_iterate_slots);
	perf_test(test_iterate_slots_extents);

	if (!j.fn) {
		pr_err("unknown test %s", testname);
		return;
	}

	//pr_info("running test %s:", testname);

	if (nr_threads == 1)
		btree_perf_test_thread(&j);
	else
		for (i = 0; i < nr_threads; i++)
			kthread_run(btree_perf_test_thread, &j,
				    "bcachefs perf test[%u]", i);

	while (wait_for_completion_interruptible(&j.done_completion))
		;

	time = j.finish - j.start;

	scnprintf(name_buf, sizeof(name_buf), "%s:", testname);
	bch2_hprint(nr_buf, nr);
	bch2_hprint(per_sec_buf, nr * NSEC_PER_SEC / time);
	printk(KERN_INFO "%-12s %s with %u threads in %5llu sec, %5llu nsec per iter, %5s per sec\n",
		name_buf, nr_buf, nr_threads,
		time / NSEC_PER_SEC,
		time * nr_threads / nr,
		per_sec_buf);
}

#endif /* CONFIG_BCACHEFS_TESTS */
