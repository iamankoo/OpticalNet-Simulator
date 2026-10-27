# OpticalNet — Architecture

OpticalNet is a software simulation platform for modeling and validating optical communication networks. It is a **C++20 simulation engine** with a thin Python/FastAPI layer on top. All components described below are implemented (Phases 1-10); `summary.md` records how each was verified. Phase numbers refer to [`Phases.md`](Phases.md). Technologies are defined in [`TECHSTACK.md`](TECHSTACK.md).

## High-level architecture

```text
                      HTTP client
                        │
                        ▼
                FastAPI / Pydantic                (Phase 9)
                        │
                        ▼
                Python package `opticalnet`       (Phase 9)
                        │
                        ▼
                 pybind11 bindings                (Phase 9)
                        │
                        ▼
                OpticalNet C++ Core
                        │
        ┌───────────────┼────────────────┐
        ▼               ▼                ▼
   Core Domain      Topology Engine   Routing Engine
   (Phase 1)         (Phase 2)         (Phase 3)
        │               │                │
        └───────────────┼────────────────┘
                        ▼
                Resource Manager                  (Phase 4)
                        │
                        ▼
                Simulation Engine                 (Phase 5)
                        │
                        ▼
                 Worker Pool                      (Phase 6)
                        │
                        ▼
              Failure & Recovery (overlay)        (Phase 7)
                        │
                        ▼
              Validation Framework                (Phase 8)
```

The diagram is a dependency/layering view: each layer uses those above it in the C++ core. Validation is cross-cutting — it inspects every layer. Phase 10 added no component: it added benchmarks and three measured internal optimizations (see *Performance notes*).

## Key relationships

```text
Network
 ├── Nodes
 ├── FiberLinks
 ├── Transceivers
 └── SwitchingElements

ConnectionRequest
        ↓
ConnectionManager  (connection lifecycle; owns the ResourceManager)
        ↓
RoutingEngine  (reads Topology; asks "is this link usable?" through RoutingConstraints::linkFilter)
        ↓
Path
        ↓
ResourceManager  (allocate / release channels per link; atomic)
        ↓
Active Connection  (request + exact path + demand)  →  driven over virtual time by the SimulationEngine (Phase 5)
```

Who owns what:

```text
Topology           = static structure (referenced by the others, never owned by them)
RoutingEngine      = path calculation, stateless
ResourceManager    = dynamic resource state (channels allocated per link)
ConnectionManager  = connection lifecycle (registry, establish, release)
```

## Subsystems

### Core domain model (Phase 1)
Plain C++ classes in namespace `opticalnet` with validated constructors and strongly typed IDs. No dependency on any other subsystem.

- **Network model:** `Network` owns all elements and is the single source of structural truth.
- **Node model:** `Node` — ID, name, optional attached `SwitchingElement`, transceivers.
- **FiberLink model:** `FiberLink` — endpoints, length, attenuation, capacity (number of wavelength channels/slots). Static properties only; usage lives in the `ResourceManager`.
- **Transceiver model:** `Transceiver` — data rate and optical reach, used for feasibility.
- **Switching element model:** `SwitchingElement` — port count and switching constraints (ROADM/OXC style).

Static structure (`Network`) is deliberately separated from dynamic state (`ResourceManager`, the per-run simulation state), so a topology can be shared read-only across concurrently running replications (Phase 6).

### Topology / graph representation (Phase 2, implemented)
`Topology` owns the single `Network` and maintains adjacency lists beside it (Node = vertex, FiberLink = edge, link cost = weight). All mutation goes through `Topology`, so the index cannot diverge from the `Network`, which is exposed read-only. There is no separate builder class: `Topology`'s add/remove operations (or `loadTopologyFromJson`) are the construction path.

- **Link direction:** links are **bidirectional by default** (a physical fiber pair carries both directions); a link may be marked **`Directed`** (source to target only). `outgoing(node)` lists edges traversable *from* a node (a bidirectional link appears at both ends, a directed link at its source only); `incident(node)` lists every touching link regardless of direction. Phase 3 routing consumes `outgoing()` and never needs to interpret direction itself.
- **Parallel links** (distinct `LinkId`s between the same nodes) are allowed; **self-links** are rejected.
- **Cost:** `FiberLink::administrativeCost` (positive, default 1). `CostMetric` (`HopCount`, `Distance`, `Administrative`) + `linkCost()` select the edge weight; routing chooses the metric.
- **Connectivity:** `isReachable` (direction-aware), `connectedComponents`/`isConnected` (weak, physical), `isStronglyConnected`.
- **Validation:** structural integrity (endpoints exist, unique ids, no self-links) is guaranteed by construction. `validate()` reports whole-topology rules: node degree vs. switching-element port count (Error), isolated nodes and disconnected topology (Warnings). "Valid" and "connected" are separate questions.
- **JSON loading/export:** `loadTopologyFromJson/File`, `topologyToJson` (schema documented in `TopologyIo.hpp`; strict, unknown keys rejected). After validation the topology is treated as immutable during a simulation, except for failure state (see below).

### Routing engine (Phase 3, implemented)
```text
Topology ──► RoutingEngine ──► Path ──► (Phase 4) ResourceManager
```
`RoutingEngine` holds an `IRoutingAlgorithm` (`DijkstraRouter` by default) and is handed the `Topology` on every call; it never owns the topology, never owns resource state and never reserves anything. `findPath(topology, source, destination, RoutingConstraints)` returns a `Result<Path>`.

- **Dijkstra** runs over `Topology::outgoing()` with a binary heap, so directed links are honoured and parallel links are separate edges. Ties break deterministically (lower cost, fewer hops, smaller predecessor `NodeId`, smaller `LinkId`).
- **Constraints** take part in the search: `maxCost` (in the units of the selected `CostMetric`; optical reach is a `Distance` limit), `maxHops` (exact), minimum static link capacity, blocked nodes/links, and an optional `linkFilter` predicate. The filter is the seam for Phase 4: the resource manager supplies "link has free capacity" without routing holding any resource state. Failed elements (Phase 7) will be passed the same way.
- **Errors:** `NotFound` (unknown endpoint), `InvalidArgument` (bad constraints), `NoRoute` (unreachable), `NoFeasibleRoute` (reachable but infeasible under the constraints). `source == destination` yields the trivial path (one node, no links, cost 0).

### Path representation (Phase 3, implemented)
`Path` — ordered node sequence, ordered **link** sequence (the actual fibers, which matter with parallel links), total cost, the `CostMetric` it is expressed in, and hop count. Immutable value type returned by routing and consumed by the resource manager.

### Resource manager (Phase 4, implemented)
`ResourceManager` stores only the **allocated** channel count per link; each link's total comes from the `Topology` on demand, so static capacity lives in one place. A connection of demand N uses N channels on every link of its path; a bidirectional link's channels are one shared pool. `allocate(links, demand)` and `release(links, demand)` are atomic: every link is validated first and state is changed only if all checks pass (no rollback needed). `validate()` reports allocation above capacity or on links the topology no longer has. Single-threaded in Phase 4; Phase 6 adds synchronisation around it.

### Connection/request lifecycle
```text
Requested ──route ok──► Routed ──allocate ok──► Established ──depart──► Released
    │                      │
    └── no path ──► Rejected   └── no capacity ──► Rejected
Established ──element failure──► Rerouted | Dropped      (Phase 7)
```
`ConnectionRequest` is the input; `Connection` is the established record (request, the exact path, demand). Phase 4 implements establish and release: `ConnectionManager::establish` routes with a capacity-aware `linkFilter`, allocates the path atomically, then registers the connection; `release` returns exactly the stored links and demand. Connections have no automatic expiry (departure scheduling is Phase 5), and rerouting after failures is Phase 7.

### Simulation engine (Phase 5, implemented)
```text
SimulationConfig
       ↓
SimulationEngine
       │
       ├── EventQueue
       ├── RequestGenerator ── Rng (std::mt19937_64, seeded)
       ├── ConnectionManager ── RoutingEngine + ResourceManager
       └── Metrics  →  SimulationResult

EventQueue → ConnectionArrival → ConnectionManager → Routing + Resources → Connection → ConnectionRelease
```
A discrete-event simulation in **virtual time**: the clock is a number that jumps from event to event, and nothing sleeps or reads wall-clock time. `ConnectionArrival` runs `ConnectionManager::establish` and, if accepted, schedules a `ConnectionRelease` at arrival + lifetime; `ConnectionRelease` releases exactly the stored connection. Events are ordered by (time, type, sequence); at equal times releases precede arrivals. The engine is stateless: each run builds its own `ConnectionManager` and only reads the `Topology`, so no active-connection state is ever stored in the static structure. `SimulationEngine::run` generates the workload; `runScript` replays an explicit list of arrivals.

- **Workload:** `RequestGenerator` draws every random quantity of a request (arrival gap, endpoints, demand, lifetime) when the request is generated, so the workload is independent of how the network reacts. Arrivals are Poisson (exponential gaps, mean 1/rate) or fixed; lifetimes exponential or fixed; endpoints uniform and never equal.
- **Determinism:** a fixed seed reproduces the same workload, event order, outcomes and metrics (**deterministic mode**).
- **Metrics:** request counts and rejection reasons, acceptance/blocking rates, peak and final allocation, time-weighted average utilization, path cost/hops, active/peak connections, releases. Formulas are in `summary.md` and `SimulationResult.hpp`.
- Single-threaded in Phase 5; Phase 6 adds concurrent workers.

### Multithreaded simulation: simulation study and worker pool (Phase 6, implemented)
**Phase 6 parallelizes independent simulation replications; each individual simulation event loop remains single-threaded.**

```text
                    Simulation Study (StudyConfig: replications, workers, base seed)
                           │
                    Study Coordinator  (derives one seed per replication, collects results by index)
                           │
                       ThreadPool
                    ┌──────┴───────┐
                    ▼              ▼
              Simulation       Simulation        … one task per replication
                Run A            Run B
                    │              │
              Event Loop      Event Loop         (single-threaded Phase 5 engine)
                    │              │
             ConnectionMgr   ConnectionMgr
                    │              │
              Resources       Resources
```
- **Shared (read-only):** the `Topology` and the `SimulationConfig`. During a study the topology is immutable and read concurrently; it has no locks and must not be mutated meanwhile.
- **Per replication (never shared):** `Rng`, `EventQueue`, `RequestGenerator`, `ConnectionManager`, `ResourceManager`, metrics, result. There is no global or shared mutable simulation state, so no locking is needed around the simulation itself.
- **`ThreadPool`:** `std::thread` workers, a `std::mutex`/`std::condition_variable` guarded FIFO queue, and `std::packaged_task`/`std::future` for results and exceptions. Shutdown stops accepting tasks, runs everything already queued and joins every thread; no thread is detached.
- **Seeds and determinism:** replication *i* gets `splitmix64(baseSeed + (i+1)·γ)`, computed by the coordinator, so the seed never depends on scheduling and distinct replications never share one. Results are collected by replication index, so 1 worker or 8 workers give identical outcomes.
- **Aggregation:** totals, weighted ratios, means and per-replication statistics (mean, standard deviation, standard error, 95% interval) in `StudyAggregate`.
- **Failures:** a replication error fails the whole study (lowest failing index reported); exceptions become `InternalError`. This is about *errors in the simulation run*; Phase 6 does not introduce network failure/recovery (Phase 7).

### Failure/recovery subsystem (Phase 7, implemented)
**Failures are a non-destructive overlay on the immutable topology; they never modify its structure.**

```text
Immutable Topology  +  Failure Overlay (failed links, failed nodes)  =  Current Network Availability
```

```text
                   Simulation Engine
                          │
             ┌────────────┴────────────┐
             ▼                         ▼
        Event Queue               Failure State           (one per run / replication)
             │                         │
             ▼                         ▼
      Failure / Recovery        Failed Links/Nodes
      events (scripted or          │
      seeded random)               │
             └────────────┬────────┘
                          ▼
                   Routing Engine     ← sees the overlay through the linkFilter seam (availability predicate)
                          │
                          ▼
                  Connection Manager  ← detach affected connections, re-establish (Reroute) or lose them (NoRecovery)
                          │
                          ▼
                   Resource Manager   ← dynamic allocation; configured capacity never changed
```

- `FailureState` holds the failed `LinkId`s and `NodeId`s. A link is usable iff it and both its end nodes are healthy, so a failed node disables all its incident links without deleting them. Unknown ids and repeated/impossible transitions are explicit errors.
- Failure and recovery are `EventType`s in the same event queue. At equal timestamps: **recoveries → failures → releases → arrivals**.
- `ConnectionManager::setAvailability` plugs the overlay into routing next to the capacity filter and the caller's filter; `establishOnPath` rejects unavailable links. There is no second routing algorithm.
- On a failure, the engine finds the active connections whose path uses a failed resource, **detaches** them all (releasing exactly what each held), and under `RecoveryPolicy::Reroute` **re-establishes** each, in ascending id order, through the ordinary atomic `establish` with its own id, demand and original constraints. Failure to re-establish means the connection is lost with nothing left allocated. `NoRecovery` loses affected connections outright.
- Failure schedules are scripted (validated by replay before the run) or generated by a seeded random model on its own RNG stream. Each replication of a multithreaded study has its own overlay.
- Phase 7 adds no validation framework (Phase 8), API (Phase 9) or optimisation (Phase 10).

### Metrics collection
`MetricsCollector` records accepted/blocked counts, blocking probability, link utilization, path lengths, and (Phase 7) affected/restored/dropped connections and recovery time. Metrics are plain data structs, exportable to JSON for the API.

### Validation framework (Phase 8, implemented)
Project-specific validation that **independently** checks production behaviour; it complements GoogleTest, which stays the test runner.

```text
Production Components
        │   (checked, never modified)
        ▼
Validation Layer   (opticalnet::validation + tests/validation)
        │
        ├── Invariants          topology, failure state, paths, resources, connections, simulation trace
        ├── Reference Models    exhaustive route enumeration, shadow allocation ledger, Bellman-Ford cross-check
        ├── Scenario Validation hand-computed golden fixtures, end-to-end flows, determinism / replay
        ├── Randomized Validation  seeded generators (topologies, constraints, requests, failure scripts)
        └── Cross-Layer Checks  Topology→Routing→ConnectionManager→ResourceManager, FailureState→Routing, Simulation→metrics
```
- Each checker recomputes what it verifies from raw data with different code than the component under test: adjacency from the link list, path cost link by link, allocation with a separate ledger, simulation metrics (including time-weighted utilization and same-timestamp event order) by replaying the event trace, routing optimality by exhaustive enumeration on small graphs and by Bellman-Ford on large ones.
- Checkers return a `ValidationResult` naming each violated invariant with ids and values; tests convert it to ordinary GoogleTest assertions. Randomized tests use fixed seeds, bounded loops and print the seed.
- The suite is its own executable (`opticalnet_validation_tests`) so it can be run and mutation-tested independently of the unit tests. The validation library is not linked into production targets, and a future API layer is unaffected by it.

### Python integration & FastAPI service (Phase 9, implemented)
```text
HTTP
 ↓
FastAPI            api/main.py, api/routes/*   thin routers, OpenAPI (/docs, /openapi.json)
 ↓
Pydantic           api/models.py               shape/range validation, stable error envelope
 ↓
Python API         python/opticalnet           re-exports, exception classes with stable `code`
 ↓
pybind11           src/python/bindings.cpp     module opticalnet._opticalnet
 ↓
C++ OpticalNet     topology · routing · resources · simulation · failures · study · validation
```
**Python is an interface layer; C++ remains the simulation engine.** No algorithm is duplicated in Python, and the C++ sources are unchanged by this phase.
- *Values, not objects:* the binding returns ints, floats, strings, dicts and lists. `Result<T>` becomes a Python exception per `ErrorCode` (each with a stable `code` string that FastAPI maps to an HTTP status); `StrongId<Tag>` is a range-checked `int`.
- *Ownership:* `Topology` is immutable and held by `std::shared_ptr<const Topology>`; `ConnectionSession` (one `ConnectionManager` + `FailureState`) co-owns it, so nothing outlives what it references. No raw pointer reaches Python.
- *Threading:* long C++ calls release the GIL and use only copied inputs; replications run on the C++ `ThreadPool` (Python never creates simulation threads). The only shared API object is the immutable-topology store behind one lock; connection managers, failure state and simulations are per request.
- *Build:* the extension is opt-in (`OPTICALNET_BUILD_PYTHON`), so the C++ libraries and tests build without Python or FastAPI. Details and limitations: `summary.md`.

### Configuration
JSON files (parsed with nlohmann/json) for topology and simulation parameters, validated on load into typed config structs. Topology loading exists (Phase 2); simulation configuration arrives in Phase 5. No global configuration state.

### Error handling
Expected failures (invalid topology, infeasible request, malformed config via `ErrorCode::ParseError`) are returned as typed results (`std::expected`-style `Result<T, Error>` implemented in-house for C++20) with error codes and messages. Exceptions are reserved for programming errors/invariant violations (e.g. violated preconditions). Request rejection is a normal outcome, not an error.

### Logging
A small `Logger` interface with levels (error/warn/info/debug), thread-safe sink, off or quiet by default so benchmarks and tests aren't affected. No logging framework dependency.

### Testing architecture
- GoogleTest executables per module, aggregated through CTest
- Unit tests per phase (`opticalnet_unit_tests`) and the Phase 8 validation suite (`opticalnet_validation_tests`): invariant checkers, reference/brute-force oracles, seeded randomized tests, golden fixtures, end-to-end scenarios
- Sanitizer builds (ASan/UBSan, TSan where supported) remain a possible later addition
- pytest + FastAPI `TestClient` for the Python binding and HTTP API (`tests/api`, also run by CTest as `python_api_tests` when the extension is enabled)

### Performance notes (Phase 10)
Measurements showed routing dominates simulation cost (one Dijkstra per request), so only the routing path was changed, without altering any observable behaviour:
- `Adjacency` carries a cached `const FiberLink*` into the owning `Network`'s `std::map` (map nodes never move), so Dijkstra reads a link without a lookup per edge. The pointer is ignored by `operator==`; `Topology` copies **rebuild** the index (a copy never points into the original), moves keep it valid, and `addLink` indexes the stored object. A null pointer (hand-built entry) falls back to `findLink`.
- Dijkstra keeps its labels in a contiguous vector found through an open-addressing table instead of a node-based `unordered_map`; the tie-break rules are unchanged.
- `ResourceManager` stores per-link allocations in a hash map (it is consulted for every edge by the capacity filter); every ordered output (`allocations()`, `validate()`) sorts by `LinkId`.
Determinism, thread isolation, atomic allocation and validation independence are unaffected: benchmark checksums over all computed results are identical before and after.

## Source layout

```text
include/opticalnet/   public headers (core, topology, routing, resources, sim, concurrency, failure, validation)
src/                  implementations
tests/                GoogleTest suites
src/python/           pybind11 bindings (Phase 9)
python/opticalnet/    Python package re-exporting the extension (Phase 9)
api/                  FastAPI app: main, models, errors, routes (Phase 9)
benchmarks/           C++ and Python benchmarks, report generator, RESULTS.md (Phase 10)
examples/             demo.py end-to-end walkthrough (Phase 10)
tests/api/            pytest suite for the binding and HTTP API (Phase 9)
```

## Non-goals
Distributed execution, microservices, cloud infrastructure, databases, GUIs, and physical-layer (waveform/noise) simulation. Optical effects are modeled at the level needed for routing feasibility (length, attenuation, reach, capacity).
