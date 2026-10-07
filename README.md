# OpticalNet

An optical network simulation and validation platform that models network topology, routing, resource allocation, concurrent simulation, failures/recovery, and network validation.

> **Status: Phases 1-4 of 10 complete.** The core domain model (network, nodes, fiber links, transceivers, switching elements), the topology/graph engine (adjacency lists, connectivity, validation, JSON topology loading/export) a routing engine (Dijkstra shortest path with cost metrics, hop/cost/capacity constraints and deterministic tie-breaking) and the build/test foundation exist. Resource and connection management (atomic per-link channel allocation, connection establish/release) also exists. Simulation, concurrency, failures/recovery, the validation framework and the Python/FastAPI layer are **not implemented yet**. See [summary.md](summary.md) for current progress.

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
