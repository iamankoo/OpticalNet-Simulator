# OpticalNet — Architecture

OpticalNet is a software simulation platform for modeling and validating optical communication networks. It is a **C++20 simulation engine** with a thin Python/FastAPI layer on top. Everything below is the **target design**; nothing here is implemented until `summary.md` says so. Phase numbers refer to [`Phases.md`](Phases.md). Technologies are defined in [`TECHSTACK.md`](TECHSTACK.md).

## High-level architecture

```text
                FastAPI / Python Layer            (Phase 9)
                        │
                        ▼
                 API / Integration                (pybind11 bindings)
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
              Failure & Recovery                  (Phase 7)
                        │
                        ▼
              Validation Framework                (Phase 8)
```

The diagram is a dependency/layering view: each layer uses those above it in the C++ core. Validation is cross-cutting — it inspects every layer. Phase 10 (benchmarking/release) adds no component.

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

Static structure (`Network`) is deliberately separated from dynamic state (`ResourceManager`, the per-run simulation state), so a network can be shared read-only across threads.

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

### Worker pool (Phase 6)
`WorkerPool` runs request-processing tasks on `std::thread` workers fed by a `ThreadSafeQueue` (`std::mutex` + `std::condition_variable`). Shutdown is graceful: stop accepting, drain, join.

- **Thread-safety strategy:** `Network`/`Topology` are read-only during a run; mutable shared state (`ResourceManager`, metrics) is protected by mutexes, starting coarse-grained and refined only with measurements. Route-then-allocate is treated as one critical sequence (or retried on allocation conflict) so stale availability can't cause over-allocation. The single-threaded engine remains the deterministic reference; concurrent runs are validated against invariants rather than exact event order.

### Failure/recovery subsystem (Phase 7)
`FailureModel` describes link/node failures; `FailureInjector` schedules them (explicit or seeded random) as simulation events. On failure, affected connections are located, their resources released, and rerouting attempted; unrecoverable ones are dropped. Repair events restore elements. Failed elements are an overlay on the topology (a state set consulted by routing), not structural edits.

### Metrics collection
`MetricsCollector` records accepted/blocked counts, blocking probability, link utilization, path lengths, and (Phase 7) affected/restored/dropped connections and recovery time. Metrics are plain data structs, exportable to JSON for the API.

### Validation framework (Phase 8)
Reusable checkers — resource consistency, path validity (continuity, constraints, optimality vs. reference), request-count conservation — plus integration/e2e tests with golden outputs, sanitizer runs and a CI workflow. Checkers are library code so the API can call them too.

### Python integration & FastAPI service (Phase 9)
pybind11 module exposes load-topology, configure, run, and results. A FastAPI app provides topology and simulation endpoints with Pydantic schemas. The service holds no simulation logic; it translates JSON ⇄ C++ calls. Runs are in-process and kept in memory (no database).

### Configuration
JSON files (parsed with nlohmann/json) for topology and simulation parameters, validated on load into typed config structs. Topology loading exists (Phase 2); simulation configuration arrives in Phase 5. No global configuration state.

### Error handling
Expected failures (invalid topology, infeasible request, malformed config via `ErrorCode::ParseError`) are returned as typed results (`std::expected`-style `Result<T, Error>` implemented in-house for C++20) with error codes and messages. Exceptions are reserved for programming errors/invariant violations (e.g. violated preconditions). Request rejection is a normal outcome, not an error.

### Logging
A small `Logger` interface with levels (error/warn/info/debug), thread-safe sink, off or quiet by default so benchmarks and tests aren't affected. No logging framework dependency.

### Testing architecture
- GoogleTest executables per module, aggregated through CTest
- Unit tests per phase; integration and e2e tests in Phase 8
- Reference/brute-force oracles for routing; invariant checkers for resources
- Sanitizer builds (ASan/UBSan, TSan where supported)
- pytest for the Python API (Phase 9)

## Planned source layout

```text
include/opticalnet/   public headers (core, topology, routing, resources, sim, concurrency, failure, validation)
src/                  implementations
tests/                GoogleTest suites
python/               pybind11 module + FastAPI app (Phase 9)
examples/  benchmarks/  docs/
```

## Non-goals
Distributed execution, microservices, cloud infrastructure, databases, GUIs, and physical-layer (waveform/noise) simulation. Optical effects are modeled at the level needed for routing feasibility (length, attenuation, reach, capacity).
