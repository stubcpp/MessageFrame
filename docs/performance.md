# Performance Benchmarks

*Tested on: Intel Core 7 240H, Ubuntu 22.04 (x64 Release, GCC), via
`benchmarks/benchmark.cpp`. Figures below reflect the complete end-to-end
lifecycle (parameter **addition + serialization + deserialization**). Run-to-run
variance on this hardware is roughly ±10%. Iteration counts differ per scenario
to keep total run time reasonable; each scenario lists the exact command used.*

MessageFrame adapts its storage to the parameter count: up to 128
parameters it uses a flat, allocation-free `std::vector`; beyond that it
switches to a `tsl::robin_map`, which can additionally be pre-sized via
`FrameConfig` (see [architecture.md](architecture.md#sizing-hint-via-frameconfig-optional)).

## Scenario A: small frame (4 parameters)

`--iterations 1000000 --params 4`. Header + 4 parameters, no attachment.

| Metric | Value |
|---|---|
| Avg time per message | 0.678 us |
| Throughput | 1,473,936 messages/sec (119.3 MB/sec) |
| Avg packed size | 84 bytes |
| `add` / `serialize` / `deserialize` | 0.10 us / 0.16 us / 0.32 us |

## Scenario B: peak vector streaming (127 parameters)

`--iterations 1000000 --params 127`. Header + 127 parameters — right at
the ceiling of vector-mode storage, without entering the hash map.

| Metric | Value |
|---|---|
| Avg time per message | 10.41 us |
| Throughput | 96,009 messages/sec (190.07 MB/sec) |
| Avg packed size | 2,075 bytes |
| `add` / `serialize` / `deserialize` | 2.81 us / 2.66 us / 4.54 us |

## Scenario C: large frame (150 parameters)

`--iterations 1000000 --params 150`. Header + 150 parameters — past
`SMALL_CAPACITY`, so the container has switched to hash-map mode.

| Metric | Value |
|---|---|
| Avg time per message | 22.03 us |
| Throughput | 45,402 messages/sec (107.8 MB/sec) |
| Avg packed size | 2,488 bytes |
| `add` / `serialize` / `deserialize` | 8.47 us / 3.45 us / 8.98 us |

## Scenario D: large frame with sizing hint (1024 parameters, `--reserve 1024`)

`--iterations 200000 --params 1024`, run once with `--reserve 0` and once
with `--reserve 1024`. This isolates the cost of the vector-to-map
migration that a sizing hint lets you skip.

| Metric | Without hint (`--reserve 0`) | With hint (`--reserve 1024`) | Change |
|---|---|---|---|
| Avg time per message | 219.43 us | 173.72 us | -21% |
| Throughput | 82.63 MB/sec | 104.37 MB/sec | +26% |
| Avg packed size | 19,012 bytes | 19,012 bytes | unchanged |
| `sum_add` (parameter insertion) | 88.14 us | 41.09 us | -53% |
| `sum_find` (worst case: last-inserted key) | 0.06 us | 0.06 us | unchanged |
| `sum_serialize` | 28.10 us | 27.37 us | roughly unchanged |
| `sum_deserialize` | 85.03 us | 85.10 us | roughly unchanged |

The improvement is concentrated in `sum_add`: with the hint, the container
starts directly in map mode sized for 1024 entries, so it never fills a
vector to `SMALL_CAPACITY` and migrates it. `serialize`/`deserialize` cost
is unaffected either way, since it depends only on the final parameter
count, not on how the container got there. Point lookups stay at ~60 ns
regardless of the hint, since `tsl::robin_map`'s contiguous bucket layout
gives O(1) lookups either way.
