# Architecture & Internals

## Three-part message layout

MessageFrame keeps a strict separation between routing metadata and payload,
so an intermediary can inspect a message's header without parsing the rest
of the frame.

```text
+----------------------------------------------------------------------------------------------------+
|  HEADER (fixed size: 36 bytes)                                                                     |
|  [Timestamp] [Message_Count] [Source ID] [Target ID] [Message_ID] [Message_Type] [Version] [Flags] |
+----------------------------------------------------------------------------------------------------+
|  PARAMETERS (variable size, MessagePack)                                                           |
|  [Device 1] -> [Param A: Value] [Param B: Value] [Param C: Value]                                  |
|  [Device 2] -> [Param C: Value]                                                                    |
+----------------------------------------------------------------------------------------------------+
|  ATTACHMENTS (variable size, raw bytes, append-only)                                                |
|  [Blob 1: Raw Bytes] [Blob 2: Raw Bytes] ...                                                        |
+----------------------------------------------------------------------------------------------------+
```

1. **Header (fixed size, 36 bytes).** Addressing, sequencing, and routing
   fields — id, type, source, target, counter, version, flags, timestamp.
   Can be read without touching the rest of the message.
2. **Parameters.** A key-value map encoded as MessagePack — small
   configuration state, status codes, and low-rate telemetry.
3. **Attachments.** Raw binary blobs appended as-is, outside the parameter
   map — for high-bandwidth payloads such as IQ samples or spectrum
   captures that shouldn't be routed through key/value serialization.

## Cache-friendly parameter storage (`HybridMessageMap`)

Parameters live in a flat, contiguous `std::vector<std::pair<ParameterKey,
ParameterValue>>` while their count stays at or below `SMALL_CAPACITY` (128
by default). This keeps insertion allocation-free and cache-local; for
small element counts, a linear scan is cheaper than computing a hash.

Once the count exceeds `SMALL_CAPACITY`, the container migrates to a
`tsl::robin_map` (open-addressing hash map):
1. The map is allocated on the heap.
2. Existing entries are moved from the vector into the map.
3. The container's internal `is_vector_mode` flag flips to `false`.

Because `tsl::robin_map` stores its buckets in a contiguous array rather
than chained linked-lists (unlike `std::unordered_map`), it keeps lookups
cache-friendly at scale — point lookups in a 1024-entry map measured at
roughly 60 nanoseconds in local testing (see
[performance benchmarks](performance.md)).

## Sizing hint via `FrameConfig` (optional)

`FrameConfig` doesn't move `SMALL_CAPACITY` — the vector-to-map threshold
stays fixed at 128. What it controls is which mode the container *starts*
in, for cases where you already know a message will hold many more
parameters than `SMALL_CAPACITY`:

```cpp
msgframe::FrameConfig config;
config.initial_reserve = 1024; // expected parameter count

msgframe::MessageFrame msg(
    /*msg_id=*/1001, /*msg_type=*/1, /*src_id=*/50, /*tgt_id=*/99,
    /*msg_cnt=*/1, /*proto_version=*/1, /*msg_flags=*/0, config);

// No vector fill, no vector->map migration: the map is created up front,
// sized for 1024 entries.
for (int i = 0; i < 1024; ++i) {
    msg.add("bench", ("param_" + std::to_string(i)).c_str(), msgframe::VALUE(i));
}
```

This measurably reduces insertion cost on large frames — see the
[Scenario D benchmark](performance.md#scenario-d-large-frame-with-sizing-hint-1024-parameters---reserve-1024)
for the actual numbers (a hint currently reduces `add()`-time by roughly
half on a 1024-parameter frame).

### Behavior across `clear()`

`clear()` releases the container's current storage — the vector or the map
— and re-applies the original `FrameConfig` hint (source: `HybridMessageMap::clear()`
calls `prime_storage()`, the same routine the constructor uses). In
practice:

- **Without a hint**, `clear()` returns to vector mode. If the message
  exceeded `SMALL_CAPACITY` before, it will re-migrate to map mode the next
  time it's filled past the threshold.
- **With a hint**, `clear()` goes straight back into map mode, sized for
  `initial_reserve` — the container doesn't fall back to the vector stage
  on the next fill.

Either way, `clear()` frees the previous allocation rather than reusing it
in place; the benefit of the hint is that the *next* fill doesn't pay for
a vector-to-map migration, not that the old map allocation survives.

## Project layout

```
├── include/
│   └── messageframe/
│       ├── Header.hpp             # Fixed-size message header
│       ├── Value.hpp              # Tagged-union ParameterValue (int64/double/bool/string)
│       ├── HybridMessageMap.hpp   # Vector-to-hash-map container (pImpl facade)
│       ├── Structures.hpp         # Shared types (FlatKey, Attachment, FrameConfig)
│       └── MessageFrame.hpp       # Top-level message: header + parameters + attachments
├── src/
│   ├── Header.cpp
│   ├── Value.cpp
│   ├── HybridMessageMap.cpp       # Keeps <tsl/robin_map.h> as a private implementation detail
│   └── MessageFrame.cpp
├── third_party/                   # Vendored header-only dependencies
│   ├── robin_map/                 # tsl::robin_map
│   └── msgpack/                   # MessagePack serialization/deserialization
├── examples/
│   ├── basic_usage.cpp            # Minimal demonstration of the API
│   └── extended_usage.cpp         # Extended API: add/set/update, FlatKey, FrameConfig, error handling, edge cases
├── docs/
│   ├── architecture.md            # This file
│   ├── api-guide.md               # Full usage example, add()/set()/update(), FlatKey, clear()
│   ├── installation.md            # Build and integration guide
│   ├── performance.md             # Benchmark results
│   └── for-ai-assistants.md       # Integration rules for LLM coding tools
├── benchmarks/
│   └── benchmark.cpp              # Parameterized performance benchmark (--iterations, --params, --reserve)
├── tests/
│   ├── test_framework.hpp                  # Zero-dependency test harness
│   ├── test_flat_key.cpp                   # FlatKey composition/validity tests
│   ├── test_hybrid_map.cpp                 # HybridMessageMap correctness tests
│   ├── test_message_frame.cpp              # Serialization / binary packing tests
│   └── test_messageframe_parameter_api.cpp # add()/find() over the two-key API
├── CMakeLists.txt
├── run_benchmark.sh
└── run_benchmark.bat
```
