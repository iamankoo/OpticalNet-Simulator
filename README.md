# OpticalNet

**OpticalNet is a C++20 simulator for optical (fiber) networks, with a Python/FastAPI interface.** You describe a network (nodes, fiber links, how many wavelength channels each link carries), and OpticalNet finds routes, reserves capacity for connection requests, simulates thousands of requests arriving and ending over time, breaks and repairs links and nodes while traffic is flowing, runs many independent simulations in parallel, and independently re-checks its own results. Everything is available as a C++ library, as a Python module, and as a small HTTP API with interactive Swagger documentation.

> **Status: ALL 10 PHASES COMPLETED.** See [Final status](#final-status), [Benchmark results](#benchmark-results) and [Limitations](#limitations).

## Why this exists (the real-world problem)

Telecom backbones carry internet traffic over optical fiber. Each fiber link has a limited number of channels; every connection (a "lightpath") must be given a route and reserve channels on every link it crosses. Operators and researchers need to answer questions such as:

- *Which route should this request take, and what happens when the best route is full?*
- *What fraction of requests will be **blocked** at a given load?* (the "blocking probability" planners use to size a network)
- *What happens to live traffic when a fiber is cut or a node fails — how much can be rerouted, how much is lost?*

Answering these by hand is impractical, and trusting a simulator requires evidence that it is correct. OpticalNet is a compact, deterministic, heavily validated implementation of exactly that workflow. It is a **simplified model** (channels per link; no wavelength-continuity or physical-layer impairments), intended for network-level what-if analysis and as a well-tested engineering project, not as a production planning tool.

## Architecture

```text
HTTP client (browser /docs, curl, scripts)
      │
FastAPI                  api/main.py, api/routes/*       thin routers, OpenAPI docs
      │
Pydantic                 api/models.py                   request/response validation, stable error shape
      │
Python API               python/opticalnet               plain-value API, exceptions with stable codes
      │
pybind11                 src/python/bindings.cpp         module opticalnet._opticalnet
      │
C++ OpticalNet engine
   ├─ Topology             graph, adjacency, connectivity, JSON load/export     (Phases 1-2)
   ├─ Routing              Dijkstra, metrics, constraints                       (Phase 3)
   ├─ Resources            per-link channel accounting, connection manager      (Phase 4)
   ├─ Simulation           discrete-event engine, seeded workload, metrics      (Phase 5)
   ├─ Multithreading       thread pool, parallel independent replications       (Phase 6)
   ├─ Failure & recovery   non-destructive overlay, reroute / lose              (Phase 7)
   └─ Validation           invariant checkers, independent reference models     (Phase 8)
```

Python is an **interface layer only**: no routing, allocation, simulation, failure or validation logic exists in Python. All results come from the C++ engine.

## Technology stack

C++20 (STL only), CMake + Ninja, GoogleTest/CTest, nlohmann/json (topology files), `std::thread`/`mutex`/`condition_variable` (own thread pool), pybind11 (C++/Python), Python ≥ 3.10, FastAPI, Pydantic, Uvicorn, pytest + httpx. No Boost, no databases, no Docker, no third-party graph or thread-pool libraries. Details: [TECHSTACK.md](TECHSTACK.md).

## Major features

- **Topology & graph engine** — nodes, bidirectional/directed fiber links (parallel links allowed), transceivers and switching elements, strict JSON schema (unknown keys rejected), connectivity analysis, structural validation.
- **Routing** — Dijkstra with three cost metrics (hop count, distance, administrative cost) and constraints (max cost, max hops, blocked nodes/links, minimum link capacity); deterministic tie-breaking; exact hop-limited search.
- **Resource management** — per-link channel allocation that is atomic (all links or none), connection establish/release, capacity-aware routing.
- **Simulation** — discrete-event engine in virtual time, Poisson or fixed arrivals, seeded and fully reproducible; acceptance/blocking rates, rejection reasons, time-weighted utilization, path statistics.
- **Multithreading** — independent replications run on a C++ thread pool; results are bit-identical for any worker count; aggregate statistics with standard error and 95% confidence intervals.
- **Failure & recovery** — links/nodes fail and recover (scripted or random); affected connections are rerouted (same id, demand, constraints) or lost; downtime and recovery rate reported.
- **Validation** — independent invariant checkers, exhaustive/Bellman-Ford reference models, shadow allocation ledger, trace replay, golden fixtures, randomized and stress tests; mutation-tested.
- **Python & FastAPI** — Python module and HTTP API with Swagger docs, stable error codes, request-scoped state, GIL released during long runs.

## The 10 phases

| # | Phase | What was delivered |
|---|-------|--------------------|
| 1 | Project foundation & core domain model | CMake project, strong IDs, `Result<T>` errors, Node/FiberLink/Transceiver/SwitchingElement/Network |
| 2 | Topology & graph engine | `Topology` with adjacency index, connectivity, validation report, JSON load/export |
| 3 | Routing engine | Dijkstra, metrics, constraints, deterministic tie-breaking, large-graph tests |
| 4 | Resource & connection management | `ResourceManager`, `ConnectionManager`, atomic allocate/release |
| 5 | Simulation engine | Discrete-event engine, seeded request generator, metrics |
| 6 | Multithreaded simulation | `ThreadPool`, parallel studies, derived seeds, aggregate statistics |
| 7 | Failure & recovery simulation | `FailureState` overlay, reroute/no-recovery policies, random failures |
| 8 | Validation & testing framework | Invariant checkers, reference models, generators, golden fixtures, mutation testing |
| 9 | Python + FastAPI integration | pybind11 module, Python package, FastAPI API, 102 Python tests |
| 10 | Optimization, benchmarking & release | Reproducible benchmarks, measured optimizations (about 2.2-2.7x faster routing and simulation), final validation, documentation |

All ten are **COMPLETED**. Per-phase reports are in [summary.md](summary.md).

## Benchmark results

All numbers below were **measured** by the benchmark suite in this repository, on a GitHub Codespace (4 vCPU AMD EPYC 7763, 16 GB RAM, Ubuntu 24.04, GCC 13.3 `-O3` with LTO, Release build). "Before" is the code at the end of Phase 9 (commit `44755a7`); "after" is the Phase 10 code. Each figure is the mean of two interleaved runs, each the median of three repetitions; workloads use fixed seeds and generated grid topologies and are identical in both builds. Every computed result (paths, metrics) is **bit-identical** before and after (checksum column in [benchmarks/RESULTS.md](benchmarks/RESULTS.md), which has the complete tables).

| Workload | Before | After | Speedup |
|----------|-------:|------:|--------:|
| Dijkstra, 100x100 grid (10,000 nodes, 19,800 links), 1,000 random pairs, distance metric | 2,812 ms | 1,152 ms | 2.44x |
| Same, hop-count metric | 2,523 ms | 942 ms | 2.68x |
| Same, with `maxHops` forcing the layered search | 7,927 ms | 3,457 ms | 2.29x |
| Same, with 485 blocked nodes + minimum link capacity | 2,968 ms | 1,361 ms | 2.18x |
| 3,000 capacity-aware connection establishes + releases, 30x30 grid | 1,196 ms | 466 ms | 2.57x |
| Single simulation, 20x20 grid (400 nodes, 760 links), 40,195 requests, 80,220 events | 4,536 ms | 2,004 ms | 2.26x |
| Same with random failures (608 failure events, 2,087 connections affected) | 4,979 ms | 2,254 ms | 2.21x |
| Study: 32 replications (764,405 events), 1 worker | 43.89 s | 19.17 s | 2.29x |
| Same study, 4 workers (4 threads) | 16.17 s | 6.84 s | 2.36x |
| Topology construction, 100x100 grid | 9.5 ms | 10.4 ms | 0.91x (slightly slower) |
| Topology JSON load, 3.8 MB | 79.8 ms | 81.0 ms | 0.99x |
| Validation: `checkTopology` (10,000 nodes) / trace replay (25,337 entries) | 20.6 ms / 10.2 ms | 20.6 ms / 10.3 ms | 1.00x / 1.00x |

- **Throughput after optimization:** about 870 routing queries/s on the 10,000-node grid (1.15 ms per query) and about 40,000 simulation events/s on the 400-node grid.
- **Multithreading:** on the 4-vCPU machine a 32-replication study runs 1.98x faster with 2 workers and 2.80x with 4 workers than with 1 (8 requested workers add nothing: only 4 vCPUs). Results are identical for any worker count. Over real HTTP, 16 simulation requests sent from 8 concurrent clients finish 2.8x faster than sent one after another (the GIL is released while C++ runs).
- **API (real uvicorn server, loopback):** `GET /health` 0.86 ms, `POST /route` 1.30 ms per request (the engine itself takes 0.09 ms of that), `POST /simulate` of ~20,000 events 552 ms, `POST /study` of 16 replications 5.3 s with 1 worker vs 1.9 s with 4 workers.
- **Memory:** peak resident memory is 16.2 MiB for the 10,000-node topology workloads and 18.6 MiB for the simulation workload (17.8 MiB before; the cached link pointers cost +0.8 MiB there).
- **What got faster and why:** the workloads showed routing dominating everything (a simulation is mostly Dijkstra). The wins came from (1) caching a pointer to each link in its adjacency entry instead of a `std::map` lookup per edge, (2) a flat open-addressing label table in place of a node-based `unordered_map`, and (3) a hash map for per-link allocations (consulted by the capacity check inside routing). The price: topology construction is up to ~10% slower and neighbour queries up to ~19% slower (both well under 1 ms to 10 ms in absolute terms), because of the extra pointer per edge and because copies now rebuild the index. **Not claimed:** nothing here says OpticalNet is "production ready" or "enterprise scale"; the largest graph tested is 10,000 nodes.

## Tests

All counts are from the final run in the Codespace (GCC 13.3, Ubuntu 24.04), Debug and Release, with warnings treated as errors:

| | Count |
|---|---:|
| C++ unit tests (`opticalnet_unit_tests`) | 501 |
| C++ validation tests (`opticalnet_validation_tests`) | 105 |
| **Total C++ tests** | **606** (596 from Phases 1-9 + 10 new in Phase 10) |
| Python binding + FastAPI tests (pytest) | 102 |
| **CTest entries** (606 C++ + `python_api_tests`, which runs the 102 pytest cases) | **607** |

Debug: 607/607 passed. Release: 607/607 passed. Earlier phases were developed and tested with MSVC 19.44 on Windows. After the optimizations the full pre-existing suite (597 CTest entries) passed on Windows Release and the 10 new tests passed there too; the complete final run (all 607, Debug and Release) was done on Linux/GCC only.

## Example topology

`configs/ring4.json` is a four-node ring (100 km spans, 40 channels per link); `configs/nsfnet.json` is the 14-node, 21-link NSFNET backbone. Minimal inline example:

```json
{"nodes": [{"id": 1}, {"id": 2}, {"id": 3}],
 "links": [{"id": 1, "source": 1, "target": 2, "length_km": 100, "capacity_channels": 4},
           {"id": 2, "source": 2, "target": 3, "length_km": 150, "capacity_channels": 4}]}
```

Links are bidirectional unless `"direction": "directed"`. Unknown keys are rejected by the C++ parser.

## Example: routing

```python
import opticalnet
topology = opticalnet.load_topology("configs/ring4.json")
print(opticalnet.route(topology, 1, 3, metric="distance"))
# {'nodes': [1, 2, 3], 'links': [1, 2], 'cost': 300.0, 'hop_count': 2, 'metric': 'distance'}
```

Over HTTP: `POST /route {"topology_id": "...", "source": 1, "destination": 3, "metric": "distance", "max_hops": 3}` returns `{"success": true, "path": {...}}`; an impossible request returns `{"error": {"code": "NO_FEASIBLE_ROUTE", "message": "..."}}` (HTTP 404).

## Example: simulation

```python
config = opticalnet.SimulationConfig()
config.seed, config.end_time, config.arrival_rate, config.mean_lifetime = 42, 200.0, 2.0, 10.0
result = opticalnet.simulate(topology, config)
m = result["metrics"]
print(m["total_requests"], m["acceptance_rate"], m["average_utilization"])
```

The same seed always produces the identical result (all metrics, and the event trace if requested).

## Example: failure / recovery

```python
config.failure_schedule = [opticalnet.ScriptedFailure(5.0, "fail_link", 1), opticalnet.ScriptedFailure(20.0, "recover_link", 1)]
config.recovery_policy = "reroute"          # or "no_recovery"
arrivals = [opticalnet.ScriptedArrival(time=1.0, connection_id=1, source=1, destination=3, lifetime=100.0)]
m = opticalnet.simulate(topology, config, arrivals)["metrics"]
print(m["connections_affected"], m["connections_rerouted"], m["connections_lost"], m["total_connection_downtime"])
```

Failures are an overlay — the topology is never modified. A connection hit by a failure is detached, then re-established under the same id, demand and constraints if a route still exists; otherwise it is lost and its resources are released.

## Example: multithreaded study

```python
study = opticalnet.run_study(topology, config, replications=16, base_seed=7, workers=4)
a = study["aggregate"]["acceptance_rate"]
print(a["mean"], a["confidence_interval_95"], study["workers_used"])
```

Replication *i* always uses the seed derived from `(base_seed, i)`, so `workers=1` and `workers=4` give identical numbers.

## Python / FastAPI usage

| Endpoint | Purpose |
|----------|---------|
| `GET /health` | liveness |
| `POST /topologies`, `GET /topologies/{id}` (+ `/nodes`, `/links`, `/export`), `DELETE` | submit and inspect topologies (C++ parses them) |
| `POST /route` | shortest feasible path with metric and constraints |
| `POST /connections/scenario` | establish/release connections, fail/recover links or nodes (request-scoped state) |
| `POST /simulate` | one simulation (generated or scripted workload, failures, trace) |
| `POST /study` | multithreaded replications with confidence intervals |
| `POST /validate` | C++ topology and trace validation |

Errors always look like `{"error": {"code": "...", "message": "..."}}` with stable codes: `NOT_FOUND`, `NO_ROUTE`, `NO_FEASIBLE_ROUTE` (404); `INVALID_ARGUMENT`, `PARSE_ERROR`, `VALIDATION_ERROR` (422); `DUPLICATE_ID`, `INSUFFICIENT_CAPACITY`, `INVALID_TRANSITION`, `RESOURCE_UNAVAILABLE`, `CONSTRAINT_VIOLATION` (409); `INTERNAL_ERROR` (500). Interactive docs: `/docs`; schema: `/openapi.json`.

## How to build and run

Requirements: a C++20 compiler (MSVC 2022 or GCC ≥ 11), CMake ≥ 3.20, Ninja, Python ≥ 3.10 with development headers (for the optional extension), Git. GoogleTest, nlohmann/json and pybind11 are downloaded by CMake.

CMake options: `OPTICALNET_BUILD_PYTHON` (extension, default OFF), `OPTICALNET_BUILD_BENCHMARKS` (default OFF), `OPTICALNET_BUILD_TESTS` (default ON), `OPTICALNET_WARNINGS_AS_ERRORS` (default ON). The C++ libraries and tests build without Python or FastAPI. The exact commands for Windows/VS Code and for GitHub Codespaces are in the next section.

## Recruiter Demo

### A. Windows + VS Code (PowerShell)

Prerequisites: Windows, VS Code, Git, Visual Studio Build Tools 2022 (C++ workload), Python 3.10+, CMake, Ninja (in the tested setup CMake and Ninja came bundled with the Build Tools).

```powershell
# 1. Clone and open
git clone https://github.com/iamankoo/OpticalNet-Simulator.git
cd OpticalNet-Simulator
code .

# 2. In VS Code: Terminal > New Terminal (PowerShell). Load the MSVC compiler environment into this terminal.
#    (I built with vcvars64.bat; Launch-VsDevShell.ps1 is the PowerShell equivalent. If you installed full Visual Studio
#    instead of Build Tools, the path starts with "C:\Program Files\Microsoft Visual Studio\2022\<Edition>".)
& "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\Launch-VsDevShell.ps1" -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
#    cmake and ninja must be on PATH; the Build Tools bundle them under ...\Common7\IDE\CommonExtensions\Microsoft\CMake

# 3. Python dependencies
python -m pip install -r requirements.txt

# 4. Configure + build Release (C++ library, tests, Python extension, benchmarks)
cmake -S . -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release -DOPTICALNET_BUILD_PYTHON=ON -DOPTICALNET_BUILD_BENCHMARKS=ON
cmake --build build/release

# 5. Run all tests (606 C++ tests + the 102 Python tests as one CTest entry = 607)
ctest --test-dir build/release --output-on-failure -j4

# 6. Optional: Debug build and tests (slower; several minutes)
cmake -S . -B build/debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DOPTICALNET_BUILD_PYTHON=ON "-DOPTICALNET_PYTHON_OUTPUT_DIR=$PWD/build/debug/python/opticalnet"
cmake --build build/debug
ctest --test-dir build/debug --output-on-failure -j4

# 7. Start the API (leave this terminal running)
python -m uvicorn api.main:app --reload
```

Open a **second** terminal (Terminal > New Terminal) for the following. Windows paths use `\`.

```powershell
# 8. Swagger UI in the browser:  http://127.0.0.1:8000/docs   (schema: /openapi.json)

# 9. Load the ring4 topology (returns a topology_id)
$t = Invoke-RestMethod -Method Post -Uri http://127.0.0.1:8000/topologies -ContentType application/json -InFile configs\ring4.json
$id = $t.topology_id; $t

# 10. Route
Invoke-RestMethod -Method Post -Uri http://127.0.0.1:8000/route -ContentType application/json -Body (@{topology_id=$id; source=1; destination=3; metric="distance"} | ConvertTo-Json)

# 11. Simulate
Invoke-RestMethod -Method Post -Uri http://127.0.0.1:8000/simulate -ContentType application/json -Body (@{topology_id=$id; config=@{seed=42; end_time=200; arrival_rate=2; mean_lifetime=10}} | ConvertTo-Json -Depth 5)

# 12. Failure/recovery, multithreaded study and validation, all in one narrated script:
python examples\demo.py

# 13. Benchmarks (Release build)
.\build\release\benchmarks\opticalnet_bench.exe --quick
python benchmarks\bench_api.py --quick
```

`examples\demo.py` talks to the running server; `python examples\demo.py --in-process` runs the same steps without a server.

### B. GitHub Codespaces (Linux)

Create a Codespace on the repository (Code > Codespaces > Create). The default image has GCC, CMake and several Pythons; install Ninja and use a Python 3.10-3.13 environment:

```bash
sudo apt-get update && sudo apt-get install -y ninja-build
/usr/local/python/3.13.8/bin/python3 -m venv .venv && source .venv/bin/activate    # any Python 3.10-3.13 works
pip install -r requirements.txt

cmake -S . -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release -DOPTICALNET_BUILD_PYTHON=ON -DOPTICALNET_BUILD_BENCHMARKS=ON -DPython_EXECUTABLE=$(which python)
cmake --build build/release
ctest --test-dir build/release --output-on-failure -j4                # 607 tests

python -m uvicorn api.main:app --host 0.0.0.0 --port 8000 &           # Codespaces forwards the port: Ports tab > open in browser, add /docs
python examples/demo.py                                               # narrated: topology, routing, connections, simulation, failure/recovery, study, validation
./build/release/benchmarks/opticalnet_bench --quick
python benchmarks/bench_api.py --quick
```

### C. The 5-minute demo flow

1. **(1 min) Build and prove it works:** `ctest` — look for `100% tests passed, 0 tests failed out of 607`.
2. **(1 min) Start the API and open `/docs`:** every endpoint has typed request/response models and documented error responses.
3. **(1 min) `python examples\demo.py`:** step 3 (the three metrics pick different paths on NSFNET), step 5 (a connection is rejected with `INSUFFICIENT_CAPACITY`, then accepted after a release), step 6 (the same seed gives an identical result).
4. **(1 min) Failure/recovery (demo step 7):** the same single connection is *rerouted* under policy `reroute` and *lost* (with downtime) under `no_recovery`.
5. **(1 min) Study + validation (demo steps 8-9):** results identical for 1 and 4 workers; validation replays the simulation trace with independent code and reports no failures. Finish with `opticalnet_bench --quick` to see the measured speed.

## Project structure

```text
include/opticalnet/    public C++ headers: core, topology, routing, resources, failures, simulation, concurrency, validation
src/                   C++ implementations (+ src/python: pybind11 bindings)
python/opticalnet/     Python package (re-exports the compiled extension)
api/                   FastAPI app: main.py, models.py, errors.py, store.py, convert.py, routes/
tests/unit/            GoogleTest unit tests (501)       tests/validation/  validation suite + golden fixtures (105)
tests/api/             pytest: binding, HTTP API, error mapping, end-to-end (102)
benchmarks/            Bench.cpp (C++), bench_api.py, bench_server.py, report.py, RESULTS.md
examples/              demo.py (end-to-end walkthrough)
configs/               ring4.json, nsfnet.json
Phases.md  ARCHITECTURE.md  TECHSTACK.md  summary.md   project documentation
```

## Limitations

- **Model scope:** channel counts per link only — no wavelength continuity, spectrum assignment, or physical-layer impairments. Routing is single-path shortest-feasible; no protection paths or multi-path optimization.
- **Scale:** the largest topology exercised is 10,000 nodes / 19,800 links (routing, load, validation) and 400 nodes for full simulations. Larger scales were not measured.
- **Platforms:** the final full test and benchmark run was on Linux/GCC 13 (Codespace). Earlier phases were built with MSVC 19.44 on Windows; after the Phase 10 changes only the pre-existing Windows Release suite and the new optimization tests were re-run there. The GCC-driven test-code fixes were not re-compiled with MSVC, and the PowerShell commands in the demo section (`Launch-VsDevShell.ps1`, `Invoke-RestMethod`) were not executed by me in exactly that form. Clang was not tested. The Python extension must be built for the Python interpreter that will use it (no wheel packaging).
- **API:** no authentication (local service); topologies are stored in memory (max 64); `linkFilter` callbacks are not exposed to Python; topologies are immutable once loaded. The in-process `TestClient` adds ~3.5 ms per request, so timings through it understate server speed; use `benchmarks/bench_server.py` for real-server numbers.
- **Benchmarks:** from one cloud VM (shared hardware, 4 vCPU), so absolute numbers vary by machine; thread scaling flattened at 4 workers. Topology construction and neighbour queries are slower after the optimization (up to ~10% and ~19%, small absolute times). No profiler output beyond timing was collected. Memory figures are process peak resident size.
- **Testing:** randomized tests are bounded, not formal proofs; no sanitizers or coverage tooling were run. Six mutation experiments on the Phase 10 changes: 4 detected, 2 equivalent (see [summary.md](summary.md)).

## Future improvements

Protection/backup paths and k-shortest-path routing; wavelength/spectrum assignment; sanitizer and coverage CI (GitHub Actions); prebuilt wheels; a persistent result store and a small web UI; further routing work on the remaining heap and adjacency-lookup cost; larger-scale benchmarks.

## Final status

```text
Phase 1  COMPLETED     Phase 6   COMPLETED
Phase 2  COMPLETED     Phase 7   COMPLETED
Phase 3  COMPLETED     Phase 8   COMPLETED
Phase 4  COMPLETED     Phase 9   COMPLETED
Phase 5  COMPLETED     Phase 10  COMPLETED

OpticalNet - ALL 10 PHASES COMPLETED
```

## Documentation

| Document | Purpose |
|----------|---------|
| [Phases.md](Phases.md) | The 10-phase roadmap and each phase's definition of done |
| [ARCHITECTURE.md](ARCHITECTURE.md) | System architecture |
| [TECHSTACK.md](TECHSTACK.md) | Technology stack |
| [summary.md](summary.md) | Per-phase implementation and validation reports |
| [benchmarks/RESULTS.md](benchmarks/RESULTS.md) | Complete benchmark tables (generated from raw measurements) |
