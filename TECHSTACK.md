# OpticalNet — Tech Stack

The stack is locked. OpticalNet is primarily a **C++ simulation engine**; the core implementation stays in C++. Python is an integration layer only.

## Locked stack

| Area | Technology | Why | Where |
|------|-----------|-----|-------|
| Language | C++20 | Performance, strong typing, concurrency support | Entire core (Phases 1–8, 10) |
| Library | STL | Containers, algorithms, `<random>`, `<chrono>`; no extra deps | Core |
| Paradigm | Object-Oriented Programming | Natural fit for nodes, links, elements, routing strategies | Domain model, interfaces |
| Build | CMake | Standard cross-platform C++ build | Whole repo |
| Algorithms | Graph algorithms, Dijkstra, constraint-aware routing, resource allocation | The simulated problem itself | Phases 2–5 |
| Concurrency | `std::thread`, `std::mutex`, `std::condition_variable`, `std::packaged_task`/`std::future`, `std::atomic` (tests), worker pool | Standard-library-only concurrency; `ThreadPool` runs independent simulation replications in parallel | Phase 6 (`opticalnet_concurrency`; linked via CMake `Threads::Threads`) |
| C++ testing | GoogleTest, CTest | Unit/integration tests, single test entry point | All phases |
| Python | Python 3 | Integration/API layer | Phase 9 |
| API | FastAPI | Simple, typed HTTP API | Phase 9 |
| Schemas | Pydantic | Request/response validation | Phase 9 |
| C++/Python integration | pybind11 | Direct in-process bindings; avoids IPC/serialization overhead; CMake-friendly | Phase 9 |
| Config parsing | nlohmann/json 3.11.3 (header-only, via `FetchContent`) | Topology/simulation config files need a JSON parser; avoids hand-written parsing | Topology loading/export in Phase 2 (`opticalnet_topology`, private dependency); simulation config in Phase 5 |

pybind11 and nlohmann/json are the only non-spec additions, each justified by a concrete requirement. Both are fetched via CMake `FetchContent`, as is GoogleTest.

## Build requirements

- C++20 compiler: GCC ≥ 11, Clang ≥ 14, or MSVC 2022
- CMake ≥ 3.20
- Git (FetchContent downloads dependencies)
- Python ≥ 3.10 with pip (Phase 9 only)

## Development tools

- CMake + Ninja or Make/MSBuild
- clang-format / clang-tidy (optional, style and static checks)
- AddressSanitizer / UBSan, and ThreadSanitizer where supported (Phase 6+)
- pytest + `httpx` (Phase 9 API tests)

## Testing tools

GoogleTest (C++), CTest (runner), pytest (Python API only).

## Must NOT be introduced

- Distributed systems, microservices, message brokers
- Docker/Kubernetes/cloud infrastructure (a Dockerfile is acceptable only if later explicitly requested)
- Databases (results are files/in-memory)
- Frontend frameworks or UIs
- Boost or other heavy libraries without a demonstrated need
- Python reimplementations of core logic
- Third-party thread-pool or graph libraries (the point is to implement them)
