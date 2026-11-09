# Priority experiments

The primary outcome is the client's priority-weighted score (lower is better),
with 100% reliability required. Total wall time and latency by priority are
separate outcomes; a lower weighted score does not imply a shorter whole test.

## Repeat the main comparison

The original batch on 2026-11-07 compared `fifo`, `priorityQueue`, and
`priorityChunked` without a cache. Its frozen sources and results are preserved
in `results/batch-20261107-134247Z/`. Those measurements use baseline-v1.

After merging main on 2026-11-09, compare `baseline_v2`,
`priorityQueue_cache_v2`, and `priorityChunked_cache_v2` on seeds 1, 2, and 3.
Baseline is now the root server at `baseline-v2`, with cache variant C.
Both priority variants link that exact cache via `cache_c.c`; completed hits
are answered immediately, in-flight repeats wait, and only misses are queued.
The cache is restarted empty before every measurement. Only queue ordering
and cooperative preemption differ between the three servers.

The cache retains the original request's priority: a higher-priority repeat
waiting on an unfinished low-priority original does not promote that original.
This inherited limitation is a possible follow-up experiment, not a change
included in this comparison. Other inherited cache limits (hash-only keys,
16 waiters per entry, no cleanup for unsolvable requests) are unchanged.

Keep 2 VM CPUs, 2 workers, 500 requests, difficulty 30,000,000, 20% repeats,
600,000 us delay, lambda 1.5, and the default 100,000-guess chunk size.

The agreed nine-run batch rotates the order:

| Seed | First | Second | Third |
| --- | --- | --- | --- |
| 1 | baseline_v2 | priorityQueue_cache_v2 | priorityChunked_cache_v2 |
| 2 | priorityQueue_cache_v2 | priorityChunked_cache_v2 | baseline_v2 |
| 3 | priorityChunked_cache_v2 | baseline_v2 | priorityQueue_cache_v2 |

This varies each workload across implementations. To estimate run-to-run noise
separately, repeat an identical seed and configuration with another `RUN` number.
Changing seeds alone is not replication of identical inputs.

Report this controlled three-seed batch separately from the earlier seed-5041
pilot and the baseline-v1 batch. The general `summary.csv` includes old runs
under their original variant names; `batch-summary.csv` in each new batch
contains only that batch's measurements.

`run-nine-tests.py` runs this finite batch sequentially. It refuses existing
output filenames or an already active client test, builds frozen sources in a
separate VM directory, and leaves Git commits and tags alone. It stops the idle
project server initially and its own servers between runs. It stops the batch
on failed, incomplete, incorrect, or mismatched-input results.

Each batch directory under `results/batch-*` contains:

- `status.json`: running/completed/failed state and progress.
- `plan.json` and `batch.log`: run order and execution record.
- `sources.tar.gz`, `source-hashes.json`, and `build.log`: actual build inputs.
- Server and client logs for each run.

Raw client results remain directly under `results/`, alongside `.time` and
`.metadata.json` files. Metadata identifies the frozen source archive and the
actual server/client binary hashes. The original logger's Git hash can describe
the working tree rather than the frozen build; use the batch metadata for
provenance. The batch updates `runs.csv` and `summary.csv` after each completed run.

Build with `make experiments EXP=priority` in the server VM. The normal
`experiments/run-experiment.sh` records each client run; `run-nine-tests.py`
coordinates fresh servers and frozen builds around that runner. Run its
`--check` mode before starting a batch. Local C checks are documented at the
top of the two test files; `tests/test_cache_integration.py <build-root>`
checks all three built servers, including cache hits and in-flight repeats.

Do not run another benchmark against port 5003 during the batch. Changes to
working-tree server sources do not alter the frozen binaries being measured.

## Candidate follow-up experiments (not part of the nine-run batch)

1. **Chunk-size sensitivity:** compare 10,000, 100,000, and 1,000,000 guesses.
   Hypothesis: smaller chunks reduce urgent requests' waiting time, but increase
   scheduler/lock overhead. Keep all other settings fixed. Record the compiled
   chunk size and use labels such as `priorityChunked_k10000` for result files.
   `HASH_CHUNK_SIZE` is a compile-time macro: rebuild when changing it. The
   existing Makefile supports `CFLAGS_priority_priorityChunked`; use `make -B`
   for that target because changing flags alone does not mark a binary stale.

2. **Arrival pressure:** compare delays of 600,000, 300,000, and 100,000 us for
   `priorityQueue` and `priorityChunked`. Hypothesis: preemption is most useful
   when urgent jobs arrive while workers are busy on less important work.
   Use 100 requests for a quick pilot, then validate selected settings with 500.

3. **No-priority control:** set `PRIO_LAMBDA=0`, making every request priority 1.
   Equal priorities never preempt. This measures chunking overhead without a
   priority-selection benefit. Compare score and wall time on the same seeds.
   Use separate result labels such as `priorityQueue_lambda0` and
   `priorityChunked_lambda0`.

4. **Queue implementation:** compare the sorted array with 16 FIFO queues, one
   per valid priority. This targets array moves and lock duration rather than
   the scheduling policy. Measure queue/lock overhead before treating it as the
   main performance bottleneck, and preserve FIFO within each level.

Record reliability, weighted score, wall time, and response counts/latency by
priority. Low-priority latency reveals the cost of serving urgent work first.
Few observations at a high priority are insufficient for a strong per-level
conclusion.

## Keep unlike scenarios separate

The existing summary script groups by `(variant, total, delay)`. It does not
separate lambda, repeat probability, difficulty, or chunk-size changes. The raw
filename also lacks these settings. Use distinct scenario labels for those
changes, and do not use a different `RUN` alone to distinguish scenarios.

Ensure the final measured code is committed before assigning a measured tag.
Do not rewrite old raw log headers to claim a different measured commit.

Experimental-design reference: [NIST: practical steps and process drift](https://www.itl.nist.gov/div898/handbook/pri/section1/pri14.htm).
