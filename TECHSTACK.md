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
| Concurrency | `std::thread`, `std::mutex`, `std::condition_variable`, `std::packaged_task`/`std::future`, `std::atomic` (tests), worker pool | Standard-library-only concurrency; `ThreadPool` runs independent simulation replications in parallel | Phase 6 (`opticalnet_concurrency`; linked via CMake `Threads::Threads`); Phase 7 adds no new facility (failure state uses `std::set`) |
| C++ testing | GoogleTest, CTest | Unit tests and the Phase 8 validation suite (project-specific invariant checkers, reference models, seeded randomized tests built on `<random>`/`mt19937_64`); single test entry point | All phases; validation in Phase 8 (`opticalnet_validation`, `tests/validation`) |
| Python | Python ≥ 3.10 (tested 3.10.11) | Integration/API layer only | Phase 9 (`python/`, `api/`) |
| API | FastAPI (≥ 0.115, tested 0.115.6) + Uvicorn (0.34.0) | Simple, typed HTTP API with automatic OpenAPI docs | Phase 9 (`api/`) |
| Schemas | Pydantic 2 (tested 2.13.4) | Request/response validation | Phase 9 (`api/models.py`) |
| C++/Python integration | pybind11 2.13.6 (via CMake `FetchContent`) | Direct in-process bindings; avoids IPC/serialization overhead; CMake-friendly | Phase 9 (`src/python`, CMake option `OPTICALNET_BUILD_PYTHON`, default OFF) |
| Config parsing | nlohmann/json 3.11.3 (header-only, via `FetchContent`) | Topology/simulation config files need a JSON parser; avoids hand-written parsing | Topology loading/export in Phase 2 (`opticalnet_topology`, private dependency); simulation config in Phase 5 |

pybind11 and nlohmann/json are the only non-spec additions, each justified by a concrete requirement. Both are fetched via CMake `FetchContent`, as is GoogleTest.

## Release tooling (Phase 10)

| Area | Technology | Notes |
|------|-----------|-------|
| Benchmarking | Own suite: `std::chrono` (C++), `time.perf_counter`/`http.client` (Python), `benchmarks/report.py` | No benchmarking library; fixed seeds, median of repetitions, result checksums |
| Platforms verified | MSVC 19.44 / Windows 11 (Phases 1-9, spot-check in 10); GCC 13.3 / Ubuntu 24.04 in a GitHub Codespace (final Debug + Release runs, benchmarks) | Clang not tested |
| CMake options | `OPTICALNET_BUILD_PYTHON`, `OPTICALNET_BUILD_BENCHMARKS`, `OPTICALNET_BUILD_TESTS`, `OPTICALNET_WARNINGS_AS_ERRORS` | Python and benchmarks are opt-in |

No new third-party dependency was added in Phase 10.

## Build requirements

- C++20 compiler: GCC ≥ 11, Clang ≥ 14, or MSVC 2022
- CMake ≥ 3.20
- Git (FetchContent downloads dependencies)
- Python ≥ 3.10 with pip and development files (Phase 9 only, and only when `-DOPTICALNET_BUILD_PYTHON=ON`; `pip install -r requirements.txt`). The C++ build does not need Python or FastAPI.

## Development tools

- CMake + Ninja or Make/MSBuild
- clang-format / clang-tidy (optional, style and static checks)
- AddressSanitizer / UBSan, and ThreadSanitizer where supported (Phase 6+)
- pytest + `httpx` (Phase 9 API tests; FastAPI `TestClient`)

## Testing tools

GoogleTest (C++), CTest (runner), pytest (Python binding/API only; also run by CTest as `python_api_tests` when the extension is built).

## Must NOT be introduced

- Distributed systems, microservices, message brokers
- Docker/Kubernetes/cloud infrastructure (a Dockerfile is acceptable only if later explicitly requested)
- Databases (results are files/in-memory)
- Frontend frameworks or UIs
- Boost or other heavy libraries without a demonstrated need
- Python reimplementations of core logic
- Third-party thread-pool or graph libraries (the point is to implement them)
