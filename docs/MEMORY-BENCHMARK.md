# PX3 Memory Benchmark

An offline harness that measures how much memory a PX3 Synth processor actually
costs, by creating real instances in a real process and reading the process's
own accounting. It needs no network, no DAW and no external services.

It answers:

- How much memory does one processor instance allocate?
- How much does each additional instance cost?
- How much is one-time initialisation that every instance then shares?
- How much does the editor add?
- Does memory come back when instances are destroyed?
- How does it scale from 1 to N instances?

## Running it

```bash
./scripts/memory-benchmark.sh                    # 8 instances, stress scenario
./scripts/memory-benchmark.sh --instances 16
./scripts/memory-benchmark.sh --scenario initialized
./scripts/memory-benchmark.sh --json
./scripts/memory-benchmark.sh --editor
```

The script builds the benchmark in Release if it is not already built, runs it,
prints a summary, and always saves a JSON copy under `.benchmarks/`.

To benchmark an artifact you have already built:

```bash
./scripts/memory-benchmark.sh --binary path/to/PX3MemBench
```

Or build it directly:

```bash
cmake -S . -B build/membench -G Ninja \
      -DCMAKE_BUILD_TYPE=Release -DPX3_BUILD_DIAGNOSTIC=ON
cmake --build build/membench --target PX3MemBench
build/membench/PX3MemBench_artefacts/Release/PX3MemBench --instances 16
```

**Release only.** The script refuses to run against a build directory configured
as anything else. A Debug build has different allocator behaviour and
unoptimised container layouts, so its numbers describe code nobody ships.

## Scenarios

| Scenario | What it does |
| --- | --- |
| `default` | Constructs the processor and nothing else |
| `initialized` | Adds `prepareToPlay` and a few blocks, as a host would |
| `stress` (default) | Every FX enabled, a chord playing, FX algorithms swept |

The gap between `default` and `initialized` is the interesting one: it shows how
much of the footprint is allocated at prepare time rather than at construction.

## Regression tracking

```bash
./scripts/memory-benchmark.sh --save-baseline
# ... make changes, rebuild ...
./scripts/memory-benchmark.sh --compare-baseline
```

The comparison prints a per-checkpoint delta in MB and per cent and exits `2` if
any checkpoint grew by more than the tolerance (`--tolerance`, default 5%), so
it can gate CI. Small run-to-run movement is normal and does not fail.

## What the metrics mean

| Metric | Meaning |
| --- | --- |
| **RSS** | Resident set size: physical pages mapped, including the binary and shared libraries. Starts high because the executable itself is resident. |
| **Footprint** | `phys_footprint`, what Activity Monitor shows as "Memory". Excludes clean file-backed pages, so it tracks what this process is really responsible for. **This is the number to plan with.** |
| **Heap in use** | Live bytes across every malloc zone. The closest thing to "what the plugin asked for and has not freed", and the best leak indicator. |
| **Virtual** | Reserved address space. On arm64 macOS this is hundreds of GB of mostly-unmapped range and says nothing about consumption. Reported only for completeness. |

## How the numbers are calculated

- **Baseline** is measured after JUCE has initialised and after the harness has
  reserved its own storage, so what follows is attributable to the plugin.
- Every measurement point is the **median of 5 samples taken 25 ms apart**.
  Process memory is not stable to the byte - deferred frees, lazily faulted
  pages and background threads all move it between reads. A median is steadier
  than one reading and steadier than a mean, which one outlier can drag.
- **First-instance overhead** = footprint(1 instance) − footprint(baseline).
- **Average incremental cost** = (footprint(N) − footprint(1)) / (N − 1),
  measured across the whole span rather than by averaging the per-checkpoint
  deltas. The checkpoints are unevenly spaced (1, 2, 4, 8, 16), so averaging
  them would weight the early, noisier steps far too heavily.

Results are rounded to 0.1 MB. They are not accurate to the byte and are not
presented as though they were.

## Why the first instance costs more

The first instance pays for one-time work that every later instance then shares:
JUCE's static initialisation, format and font registries, the preset library
being indexed, and lazily created singletons. Those costs are not repeated, so
the **incremental** figure is what you multiply when estimating how many
instances a session will hold - not the first-instance figure.

## Interpreting the destruction test

The report shows RSS, footprint and heap at peak, after destroying every
instance, and again after asking the allocator to release what it is holding.

Read **heap in use** and **footprint** together:

- Heap in use returning to near baseline means the plugin freed what it asked
  for. That is the leak indicator.
- Footprint and RSS staying high is normal. Allocators keep freed pages mapped
  for reuse rather than returning them to the kernel, and one-time
  initialisation is never given back.

A high footprint after destruction is therefore **not** evidence of a leak on
its own. The benchmark reports the measurements and leaves the judgement to you.

## What this does and does not measure

This harness measures **plugin processor memory in isolation**, in a process
that contains almost nothing else. Memory in a real DAW will differ because of:

- shared libraries the host has already loaded, which cost the plugin nothing
- the host's own memory, audio engine and graphics
- plugin UI resources, only while a window is open
- other plugins in the session
- host-specific behaviour such as out-of-process plugin hosting, instance
  pooling, or offline bounce modes

It is also a **process-level** measurement, not a per-object allocation count.
It cannot attribute bytes to individual objects. For that breakdown - static
struct sizes and per-voice heap estimates - run:

```bash
build/diag/PX3Diag_artefacts/RelWithDebInfo/PX3Diag memory
```

The two are complementary: `PX3Diag memory` says what the code declares, this
benchmark says what the process actually consumes, including allocator overhead
and fragmentation that no `sizeof()` can see.

## Editor benchmark

`--editor` measures editor memory **separately**; it is never folded into the
processor figures. The editor is constructed but never shown and no message loop
is pumped, so anything that only allocates once a window is on screen is not
counted. Treat it as a lower bound.
