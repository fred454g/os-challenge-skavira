/* Local queue/search tests; the server entry point is never called.
 * Run from the repository root:
 * cc -Wall -Wextra -O3 -pthread -Iinclude \
 *   experiments/priority/tests/test_priority_chunked.c src/server_helpers.c src/cache_c.c \
 *   -o /tmp/os-challenge-priority-chunked-test
 * /tmp/os-challenge-priority-chunked-test
 */
#define HASH_CHUNK_SIZE 4
#define main priority_chunked_server_main
#include "../server_priorityChunked.c"
#undef main

#include <assert.h>

static struct job make_job(int id, uint8_t prio, uint64_t start, uint64_t end,
                           uint64_t target) {
    struct job job = {
        .client_fd = id,
        .req = {.start = start, .end = end, .prio = prio},
        .next_guess = start
    };
    uint64_t little_endian_target = htole64(target);
    lonesha256(job.req.hash, (const unsigned char *)&little_endian_target,
               sizeof(little_endian_target));
    return job;
}

static void test_search_boundaries(void) {
    uint64_t answer = 999;
    struct job job = make_job(1, 1, 10, 20, 18);
    assert(search_chunk(&job, &answer) == SEARCH_MORE);
    assert(job.next_guess == 14 && answer == 999);
    assert(search_chunk(&job, &answer) == SEARCH_MORE);
    assert(job.next_guess == 18);
    assert(search_chunk(&job, &answer) == SEARCH_FOUND);
    assert(answer == 18 && job.next_guess == 19 && job.req.start == 10);

    job = make_job(2, 1, 10, 20, 20); // end is exclusive.
    assert(search_chunk(&job, &answer) == SEARCH_MORE);
    assert(search_chunk(&job, &answer) == SEARCH_MORE);
    assert(search_chunk(&job, &answer) == SEARCH_EXHAUSTED);
    assert(job.next_guess == 20);

    job = make_job(3, 1, UINT64_MAX - 5, UINT64_MAX, UINT64_MAX - 1);
    assert(search_chunk(&job, &answer) == SEARCH_MORE);
    assert(job.next_guess == UINT64_MAX - 1);
    assert(search_chunk(&job, &answer) == SEARCH_FOUND);
    assert(answer == UINT64_MAX - 1 && job.next_guess == UINT64_MAX);

    job = make_job(4, 1, UINT64_MAX - 5, UINT64_MAX, UINT64_MAX);
    assert(search_chunk(&job, &answer) == SEARCH_MORE);
    assert(search_chunk(&job, &answer) == SEARCH_EXHAUSTED);
    assert(job.next_guess == UINT64_MAX);

    job = make_job(5, 1, 10, 10, 10);
    assert(search_chunk(&job, &answer) == SEARCH_EXHAUSTED);
}

static void test_preemption_and_resume(void) {
    uint64_t answer;
    queue_push(make_job(10, 1, 10, 20, 18));
    struct job running = queue_pop();
    assert(search_chunk(&running, &answer) == SEARCH_MORE);
    assert(running.next_guess == 14);
    assert(!queue_maybe_preempt(&running)); // Nothing is waiting.

    queue_push(make_job(20, 1, 0, 1, 0));
    assert(!queue_maybe_preempt(&running)); // Equal priority does not preempt.
    queue_push(make_job(30, 5, 1, 2, 1));
    assert(queue_maybe_preempt(&running));
    assert(running.client_fd == 30 && running.next_guess == 1);
    assert(search_chunk(&running, &answer) == SEARCH_FOUND && answer == 1);

    running = queue_pop();
    assert(running.client_fd == 10 && running.next_guess == 14);
    assert(running.req.start == 10 && running.req.end == 20);
    assert(!queue_maybe_preempt(&running));
    assert(search_chunk(&running, &answer) == SEARCH_MORE);
    assert(running.next_guess == 18);
    assert(search_chunk(&running, &answer) == SEARCH_FOUND && answer == 18);

    running = queue_pop();
    assert(running.client_fd == 20); // Original FIFO order survived the pause.
    assert(queue_count == 0);
}

static void test_full_queue_swap(void) {
    uint64_t answer;
    queue_push(make_job(9000, 1, 10, 20, 19));
    struct job running = queue_pop();
    assert(search_chunk(&running, &answer) == SEARCH_MORE);

    for (int id = 0; id < QUEUE_CAPACITY; id++) {
        queue_push(make_job(id, 2, 0, 1, 0));
    }
    assert(queue_count == QUEUE_CAPACITY);
    assert(queue_maybe_preempt(&running)); // Must not wait for a free slot.
    assert(running.client_fd == 0 && running.req.prio == 2);
    assert(queue_count == QUEUE_CAPACITY);
    assert(!queue_maybe_preempt(&running));

    for (int id = 1; id < QUEUE_CAPACITY; id++) {
        struct job queued = queue_pop();
        assert(queued.client_fd == id && queued.req.prio == 2);
    }
    running = queue_pop();
    assert(running.client_fd == 9000 && running.next_guess == 14);
    assert(search_chunk(&running, &answer) == SEARCH_MORE);
    assert(search_chunk(&running, &answer) == SEARCH_FOUND && answer == 19);
    assert(queue_count == 0);
}

int main(void) {
    test_search_boundaries();
    test_preemption_and_resume();
    test_full_queue_swap();
    puts("Chunked priority tests passed (resume, priority/FIFO, full queue, interval bounds).");
    return 0;
}
