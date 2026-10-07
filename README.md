# OpticalNet

An optical network simulation and validation platform that models network topology, routing, resource allocation, concurrent simulation, failures/recovery, and network validation.

> **Status: planning.** Only documentation exists so far. No code has been implemented and nothing is buildable yet. See [summary.md](summary.md) for current progress.

## Planned design

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

Build and usage instructions will be added as phases are completed.
