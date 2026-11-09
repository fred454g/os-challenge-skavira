/* Queue-only test: includes the real queue code but never starts the server.
 * Run from the repository root:
 * cc -Wall -Wextra -O3 -pthread -Iinclude \
 *   experiments/priority/tests/test_priority_queue.c src/server_helpers.c src/cache_c.c \
 *   -o /tmp/os-challenge-priority-queue-test
 * /tmp/os-challenge-priority-queue-test
 */
#define main priority_server_main
#include "../server_priorityQueue.c"
#undef main

#include <assert.h>

static struct job make_job(int id, uint8_t prio) {
    struct job job = {
        .client_fd = id,
        .req = {.start = (uint64_t)id, .end = (uint64_t)id + 10, .prio = prio}
    };
    memset(job.req.hash, id % 256, sizeof(job.req.hash));
    return job;
}

static void expect_job(int id, uint8_t prio) {
    struct job job = queue_pop();
    assert(job.client_fd == id);
    assert(job.req.prio == prio);
    assert(job.req.start == (uint64_t)id);
    assert(job.req.end == (uint64_t)id + 10);
    for (size_t i = 0; i < sizeof(job.req.hash); i++) {
        assert(job.req.hash[i] == (unsigned char)(id % 256));
    }
}

int main(void) {
    struct job original = make_job(10, 1);
    queue_push(original);
    original.req.start = 999;
    original.req.hash[0] = 255;
    queue_push(make_job(20, 5));
    queue_push(make_job(30, 5));
    queue_push(make_job(40, 2));

    expect_job(20, 5);
    expect_job(30, 5); // Equal priorities preserve insertion order.
    expect_job(40, 2);
    expect_job(10, 1); // The queued copy was not changed through original.
    assert(queue_count == 0);

    /* Fill the bounded queue, then verify ordering and data for every job. */
    for (int id = 0; id < QUEUE_CAPACITY; id++) {
        queue_push(make_job(id, (uint8_t)(id % 16 + 1)));
    }
    assert(queue_count == QUEUE_CAPACITY);
    for (int prio = 16; prio >= 1; prio--) {
        for (int id = prio - 1; id < QUEUE_CAPACITY; id += 16) {
            expect_job(id, (uint8_t)prio);
        }
    }
    assert(queue_count == 0);

    /* The uint8_t field is safe even outside the client's usual 1-16 range. */
    queue_push(make_job(50, 0));
    queue_push(make_job(60, UINT8_MAX));
    expect_job(60, UINT8_MAX);
    expect_job(50, 0);
    assert(queue_count == 0);

    puts("Priority queue tests passed (ordering, FIFO ties, copies, capacity).");
    return 0;
}
