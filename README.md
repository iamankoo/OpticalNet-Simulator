# OpticalNet

An optical network simulation and validation platform that models network topology, routing, resource allocation, concurrent simulation, failures/recovery, and network validation.

> **Status: Phases 1-9 of 10 complete. Phase 10: not started.** The core domain model (network, nodes, fiber links, transceivers, switching elements), the topology/graph engine (adjacency lists, connectivity, validation, JSON topology loading/export) a routing engine (Dijkstra shortest path with cost metrics, hop/cost/capacity constraints and deterministic tie-breaking) and the build/test foundation exist. Resource and connection management (atomic per-link channel allocation, connection establish/release) also exists, together with a single-threaded discrete-event simulation engine (virtual time, seeded request generation, metrics). Multithreaded execution of independent simulation replications (a thread pool and parallel simulation studies) also exists. Failure and recovery simulation (link and node failures as a non-destructive overlay, rerouting of affected connections) also exists. A project-specific validation suite (independent invariant checkers, reference models, seeded randomized tests, end-to-end scenarios) also exists. A Python package (pybind11) and a FastAPI HTTP API expose the C++ engine; Python is only an interface layer. See [summary.md](summary.md) for current progress.

## Planned design (see TECHSTACK.md)

- **Core:** C++20, STL, OOP, CMake
- **Algorithms:** graph algorithms, Dijkstra shortest path, constraint-aware routing, resource allocation
- **Concurrency:** `std::thread`, `std::mutex`, `std::condition_variable`, worker pool
- **Testing:** GoogleTest, CTest
- **API:** Python 3, FastAPI, Pydantic (integration layer over the C++ core)

## Documentation

| Document | Purpose |
|----------|---------|
| [Phases.md](Phases.md) | The 10-phase development roadmap |
| [ARCHITECTURE.md](ARCHITECTURE.md) | Target system architecture |
| [TECHSTACK.md](TECHSTACK.md) | Locked technology stack |
| [summary.md](summary.md) | Living progress log |

## Topology configuration

Topologies can be described in JSON (see `configs/ring4.json` and `configs/nsfnet.json`; schema in `include/opticalnet/topology/TopologyIo.hpp`) and loaded with `loadTopologyFromFile`. Links are bidirectional by default and can be marked `"direction": "directed"`.

## Routing

`RoutingEngine::findPath(topology, source, destination, constraints)` returns the cheapest feasible `Path` (nodes, links, cost, hops) under a chosen metric (hop count, distance or administrative cost), honouring link direction and parallel links. Routing only finds paths; reserving capacity along them arrives with resource management (Phase 4).

## Resources and connections

`ConnectionManager` turns a `ConnectionRequest` into an active connection: it routes around links that lack free capacity, reserves the channels on every link of the path atomically (all links or none), and records the exact path so `release` returns precisely what was taken. `ResourceManager` holds the dynamic per-link allocation state; the `Topology` stays the static structure. This layer is single-threaded for now.

## Simulation

`SimulationEngine::run(topology, config)` simulates connection requests arriving (Poisson or fixed intervals) and expiring over **virtual time**: events are processed in order, nothing sleeps. Each accepted request is routed, allocated and later released through `ConnectionManager`. A fixed seed reproduces the run exactly. The result reports acceptance/blocking rates, rejection reasons, peak and time-averaged utilization, path length and connection counts. The engine is single-threaded for now.

## Multithreaded studies

`SimulationStudy::run(topology, config, studyConfig)` runs many independent simulation replications concurrently on a thread pool. Each replication is a complete single-threaded simulation with its own state and its own deterministically derived seed, while the topology is shared read-only. Results are returned in replication order and are identical whatever the number of worker threads; the study also reports aggregate figures (totals, weighted acceptance/blocking rates, means, standard deviations and 95% confidence intervals). Individual simulations are not parallelized.

## Failures and recovery

Links and nodes can fail and recover during a simulation, either from a scripted schedule (`SimulationConfig::failureSchedule`, e.g. "fail link 7 at t=100, recover it at t=150") or from a seeded random model. Failures are an **overlay**: the topology is never modified, a failed link or node is simply unavailable for routing and allocation, and recovering it makes it usable again. When a failure hits active connections they are rerouted under their original id, demand and routing constraints (`RecoveryPolicy::Reroute`) or lost (`NoRecovery`), with their resources released consistently. Results include failure counts, affected/rerouted/lost connections, the recovery success rate and the downtime of lost connections.

## Validation

Besides the unit tests, `opticalnet_validation_tests` validates the simulator against independent models: invariant checkers for topology, routing paths, resource accounting, connections and failure state; an exhaustive route enumerator and a shadow allocation ledger as oracles; deterministic seeded random topologies, requests and failure scripts; hand-computed golden fixtures (`tests/validation/fixtures`); end-to-end scenarios (congestion, link and node failure, recovery, parallel studies); determinism checks (same seed, 1 vs. N workers); and bounded stress tests. The checkers recompute results from raw data (for example, simulation metrics are re-derived from the event trace), and the suite was mutation-tested against 14 deliberate bugs in the production code. Run everything with `ctest --test-dir build --output-on-failure`.

## Python API

The C++ engine is available from Python through a pybind11 extension (`opticalnet._opticalnet`, wrapped by the `opticalnet` package in `python/`). Nothing is re-implemented in Python; results are plain ints, floats, strings, dicts and lists, and expected C++ errors become exceptions with stable codes (`NotFoundError`, `NoRouteError`, `InsufficientCapacityError`, ... all subclasses of `OpticalNetError`).

```python
import sys; sys.path.insert(0, "python")          # or set OPTICALNET_PYTHON_DIR
import opticalnet

topology = opticalnet.load_topology("configs/ring4.json")
print(opticalnet.route(topology, 1, 3, metric="distance"))
# {'nodes': [1, 2, 3], 'links': [1, 2], 'cost': 300.0, 'hop_count': 2, 'metric': 'distance'}

config = opticalnet.SimulationConfig()
config.seed, config.end_time, config.arrival_rate = 7, 200.0, 2.0
print(opticalnet.simulate(topology, config)["metrics"]["acceptance_rate"])
study = opticalnet.run_study(topology, config, replications=8, base_seed=3, workers=4)   # C++ thread pool
```

Build the extension (opt-in; the C++ simulator builds without Python):

```bash
pip install -r requirements.txt
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DOPTICALNET_BUILD_PYTHON=ON
cmake --build build
```

## FastAPI

`api/` is a thin FastAPI application (Pydantic models, one router per concern). It never runs algorithms itself: every request is translated into calls to the C++ engine and the result is returned as JSON. State that is mutated (connection managers, failure state, simulations) lives only for the duration of one request; only immutable topologies are stored (in memory, at most 64).

| Endpoint | Purpose |
|----------|---------|
| `GET /health` | liveness |
| `POST /topologies`, `GET /topologies/{id}`, `.../nodes`, `.../links`, `.../export`, `DELETE` | submit (strict C++ JSON schema) and inspect topologies |
| `POST /route` | shortest feasible path with metric and constraints |
| `POST /connections/scenario` | establish/release connections and fail/recover links or nodes against a request-owned C++ `ConnectionManager` |
| `POST /simulate` | one simulation (generated or scripted workload, failures, recovery policy, optional trace) |
| `POST /study` | multithreaded replications with aggregate statistics and confidence intervals |
| `POST /validate` | C++ topology and trace validation |

### Running the API

```bash
pip install -r requirements.txt
# build the extension first (see above), then, from the repository root:
uvicorn api.main:app --reload
```

Interactive documentation: <http://127.0.0.1:8000/docs>; machine-readable: <http://127.0.0.1:8000/openapi.json>.

### Example request and response

```bash
curl -X POST http://127.0.0.1:8000/route -H "content-type: application/json" \
     -d '{"topology": {"nodes": [{"id": 1}, {"id": 2}, {"id": 3}],
                       "links": [{"id": 1, "source": 1, "target": 2, "length_km": 100, "capacity_channels": 4},
                                 {"id": 2, "source": 2, "target": 3, "length_km": 150, "capacity_channels": 4}]},
          "source": 1, "destination": 3, "metric": "distance"}'
```
```json
{"success": true, "path": {"nodes": [1, 2, 3], "links": [1, 2], "cost": 250.0, "hop_count": 2, "metric": "distance"}}
```
Errors share one shape and a stable code; for example an unreachable destination returns HTTP 404:
```json
{"error": {"code": "NO_ROUTE", "message": "..."}}
```
Codes: `NOT_FOUND`, `NO_ROUTE`, `NO_FEASIBLE_ROUTE` (404); `INVALID_ARGUMENT`, `PARSE_ERROR`, `VALIDATION_ERROR` (422); `DUPLICATE_ID`, `INSUFFICIENT_CAPACITY`, `INVALID_TRANSITION`, `RESOURCE_UNAVAILABLE`, `CONSTRAINT_VIOLATION` (409); `INTERNAL_ERROR` (500).

Python/API tests: `python -m pytest tests/api -q` (or `ctest`, which includes `python_api_tests` when the extension is built).

## Roadmap

1. Project Foundation & Core Domain Model
2. Topology & Graph Engine
3. Routing Engine
4. Resource & Connection Management
5. Simulation Engine
6. Multithreaded Simulation
7. Failure & Recovery Simulation
8. Validation & Testing Framework
9. Python + FastAPI Integration
10. Optimization, Benchmarking & Release

## Build and test

Requires a C++20 compiler and CMake 3.20+. GoogleTest and nlohmann/json are downloaded automatically.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

On Windows with Visual Studio Build Tools, run these from a "x64 Native Tools" prompt (or after `vcvars64.bat`), optionally with `-G Ninja`.
