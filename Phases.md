# OpticalNet — Development Phases

OpticalNet is built in exactly **10 phases**, one at a time. A phase starts only after the previous phase's Definition of Done is fully met (no partial exits). Progress is tracked in [`summary.md`](summary.md).

| # | Phase | Status |
|---|-------|--------|
| 1 | Project Foundation & Core Domain Model | Completed |
| 2 | Topology & Graph Engine | Completed |
| 3 | Routing Engine | Completed |
| 4 | Resource & Connection Management | Not started |
| 5 | Simulation Engine | Not started |
| 6 | Multithreaded Simulation | Not started |
| 7 | Failure & Recovery Simulation | Not started |
| 8 | Validation & Testing Framework | Not started |
| 9 | Python + FastAPI Integration | Not started |
| 10 | Optimization, Benchmarking & Release | Not started |

Testing is not deferred to Phase 8: every phase ships its own unit tests. Phase 8 consolidates, extends and automates validation across the whole system.

---

## Phase 1 — Project Foundation & Core Domain Model

**Objective:** Establish the build system and the object model every later phase relies on.

**Scope:** Repository layout, CMake, C++20 setup, core domain types and interfaces, test harness. No graph algorithms, routing or simulation.

**Features/components:**
- Project structure (`include/opticalnet/`, `src/`, `tests/`, `docs`), CMake targets (library + tests), warnings-as-errors config
- Strongly typed IDs (`NodeId`, `LinkId`, …) and common types/units
- `Node`, `FiberLink` (length, capacity in wavelength channels/slots, attenuation), `Transceiver` (rate, reach), `SwitchingElement` (e.g. ROADM/OXC with port count and switching constraints)
- `Network` container owning the above
- Core interfaces/abstractions (e.g. `INetworkElement`, error/result types)

**Expected implementation:** Header/source pairs in namespace `opticalnet`, value semantics where sensible, validated constructors, no global state.

**Tests/validation:** GoogleTest + CTest wired in; unit tests for construction, invariants (e.g. negative length rejected), ID handling, `Network` add/lookup.

**Dependencies:** None.

**Definition of Done:** Clean configure/build from scratch with CMake on the target toolchain; all unit tests pass under CTest; every component listed above exists and is tested.

**Deliverables:** CMake project, core domain library, test suite, build instructions in README.

---

## Phase 2 — Topology & Graph Engine

**Objective:** Represent the network as a validated graph.

**Scope:** Graph structure, topology construction/loading, integrity checks. No routing.

**Features/components:**
- Graph representation (adjacency lists over `Network`), directed/undirected link handling
- `TopologyBuilder` for programmatic construction
- Topology loading from a configuration file (JSON)
- Node/link add/remove/query management
- Graph validation: dangling links, duplicate IDs, self-loops, connectivity, port-count consistency

**Expected implementation:** `Graph`/`Topology` classes layered over the Phase 1 model; loader reports structured errors with locations.

**Tests/validation:** Unit tests for build/mutate/query; loader tests with valid and malformed files; validation tests for each integrity rule; sample topologies (ring, mesh, NSFNET-style).

**Dependencies:** Phase 1.

**Definition of Done:** Valid topologies load and query correctly; every invalid case is detected with a clear error; tests pass.

**Deliverables:** Graph/topology library, loader, JSON schema doc, sample topology files, tests.

---

## Phase 3 — Routing Engine

**Objective:** Find feasible, cost-optimal paths over a topology.

**Scope:** Path computation only; no resource mutation (allocation is Phase 4).

**Features/components:**
- `IRoutingAlgorithm` abstraction
- Dijkstra shortest path, path reconstruction, `Path` type with total cost
- Pluggable cost metrics (hop count, distance)
- Feasibility checks (transceiver reach, element constraints)
- Capacity-aware constraints: exclude links without free capacity (read-only view of availability)

**Expected implementation:** Priority-queue Dijkstra with a constraint filter; deterministic tie-breaking.

**Tests/validation:** Known-answer tests on small graphs, unreachable destinations, tie-breaking determinism, reach/capacity constraint cases, cross-check against brute-force on small random graphs.

**Dependencies:** Phases 1–2.

**Definition of Done:** Routing returns provably shortest feasible paths on all test cases and reports infeasibility explicitly; tests pass.

**Deliverables:** Routing library, `Path` model, tests.

---

## Phase 4 — Resource & Connection Management

**Objective:** Track capacity and manage connection lifecycles consistently.

**Scope:** Single-threaded resource/connection management.

**Features/components:**
- `ConnectionRequest` (source, destination, demand) and `Connection` model
- `ResourceManager`: allocate/release wavelength channels/capacity along a `Path`, capacity tracking per link
- Connection lifecycle (requested → established → released / rejected)
- Rejection handling with reasons (no path, no capacity)
- Resource consistency validation (no over-allocation, no leaks)

**Expected implementation:** Allocation is atomic across a path (all-or-nothing, rollback on failure).

**Tests/validation:** Allocate/release round-trips return state to baseline; double release and over-allocation rejected; rollback test; consistency validator test.

**Dependencies:** Phases 1–3.

**Definition of Done:** Resource state is provably consistent after any sequence of tested operations; tests pass.

**Deliverables:** Resource manager, connection model, consistency validator, tests.

---

## Phase 5 — Simulation Engine

**Objective:** Run end-to-end, repeatable simulations of connection traffic.

**Scope:** Single-threaded simulation.

**Features/components:**
- `SimulationConfig` (topology, workload, seed, duration/request count)
- Request generation (arrival/holding-time distributions)
- Request processing: route → allocate → record outcome; release on departure
- Event/request handling (time-ordered event queue)
- Simulation lifecycle (configure, run, finish, reset)
- Metrics collection (acceptance/blocking ratio, utilization, path length)
- Deterministic mode: fixed seed gives identical results

**Tests/validation:** Same seed ⇒ identical metrics; hand-computed tiny scenarios; metrics sanity checks; resource consistency after a full run.

**Dependencies:** Phases 1–4.

**Definition of Done:** Reproducible simulations complete with correct metrics and consistent final state; tests pass.

**Deliverables:** Simulation engine, workload generator, metrics module, tests, example config.

---

## Phase 6 — Multithreaded Simulation

**Objective:** Process requests concurrently while preserving correctness.

**Scope:** Concurrency infrastructure and its integration into the simulation.

**Features/components:**
- Worker pool (`std::thread`)
- Thread-safe work queue (`std::mutex` + `std::condition_variable`)
- Concurrent request processing
- Synchronization and shared-state protection around `ResourceManager`/metrics
- Graceful worker shutdown (drain/stop, no leaked threads)

**Expected implementation:** Coarse-grained locking first; finer-grained only if measurement justifies it. Single-threaded deterministic mode remains available as the reference.

**Tests/validation:** Queue/pool unit tests; stress tests with many threads; invariants (no over-allocation, request count conservation) hold; ThreadSanitizer run where the toolchain supports it.

**Dependencies:** Phases 1–5.

**Definition of Done:** Repeated stress runs show no data races, deadlocks or invariant violations; clean shutdown verified; tests pass.

**Deliverables:** Worker pool, thread-safe queue, concurrent simulation mode, concurrency tests.

---

## Phase 7 — Failure & Recovery Simulation

**Objective:** Model network failures and evaluate recovery.

**Scope:** Failure events integrated into the simulation.

**Features/components:**
- Fiber/link failures and node failures
- Failure injection (scheduled and random, seeded)
- Recovery (element repair)
- Rerouting of affected connections
- Resource cleanup/recovery for failed connections
- Resilience metrics (affected/restored/dropped connections, restoration ratio, recovery time)

**Tests/validation:** Failure on a known path drops/reroutes as expected; no resource leaks after failure and recovery; protected topology vs. cut-vertex topology scenarios; determinism under seeded failures.

**Dependencies:** Phases 1–6.

**Definition of Done:** Failures and recoveries leave consistent state and produce correct resilience metrics in all tested scenarios; tests pass.

**Deliverables:** Failure model/injector, recovery logic, resilience metrics, tests.

---

## Phase 8 — Validation & Testing Framework

**Objective:** Consolidate and automate validation of the entire platform.

**Scope:** Fill gaps and add cross-cutting validation; per-phase unit tests already exist.

**Features/components:**
- Unit test completeness review and gap filling
- Integration tests across topology → routing → resources → simulation
- Routing validation (against reference/brute force)
- Resource-allocation validation (invariant checkers)
- Failure/recovery validation
- Concurrency validation (stress, sanitizers)
- End-to-end simulation validation with golden outputs
- Automated test execution (single CTest entry point, CI workflow)

**Dependencies:** Phases 1–7.

**Definition of Done:** One command builds and runs the full suite green; validation tools are reusable by the Phase 9 API; CI configuration present and passing.

**Deliverables:** Validation framework, integration/e2e tests, golden data, CI workflow, test documentation.

---

## Phase 9 — Python + FastAPI Integration

**Objective:** Expose the C++ engine through a Python API.

**Scope:** Integration layer and service; core logic stays in C++.

**Features/components:**
- Python bindings to the C++ core (pybind11)
- FastAPI service
- Simulation API (create/run/status)
- Network/topology API
- Pydantic request/response schemas
- Simulation result retrieval
- API validation and tests (pytest + FastAPI test client)

**Dependencies:** Phases 1–8.

**Definition of Done:** API can load a topology, run a simulation and return results identical to direct C++ runs; invalid input returns clear errors; API tests pass.

**Deliverables:** Binding module, FastAPI app, schemas, OpenAPI docs, API tests, usage docs.

---

## Phase 10 — Optimization, Benchmarking & Release

**Objective:** Measure, improve and ship.

**Scope:** Profiling-driven optimization and release preparation; no new feature scope.

**Features/components:**
- Performance optimization (profile first)
- Large-topology testing
- Simulation, routing and multithreading benchmarks
- Memory/performance analysis
- Example scenarios
- Final documentation and README completion
- Final validation (full suite, clean-checkout build)
- Release-ready project (tagged release)

**Dependencies:** Phases 1–9.

**Definition of Done:** Benchmarks reproducible with recorded results; optimizations verified to not break the test suite; docs complete; a clean clone builds and passes.

**Deliverables:** Benchmark suite and results, examples, final docs, release tag.
