# OpticalNet — Project Summary (living document)

Updated after each completed phase. Nothing is recorded here as done unless it has actually been implemented and verified.

## Project overview

OpticalNet is an optical network simulation and validation platform that models network topology, routing, resource allocation, concurrent simulation, failures/recovery, and network validation. Core in C++20; Python/FastAPI integration layer. Repository: https://github.com/iamankoo/OpticalNet-Simulator

Source-of-truth documents: [Phases.md](Phases.md), [ARCHITECTURE.md](ARCHITECTURE.md), [TECHSTACK.md](TECHSTACK.md).

## Current status

- **Implementation status:** Phase 1 (core domain model + build/test foundation) is implemented and tested. Phases 2–10 are not started.
- **Current phase:** Phase 1 complete; ready for Phase 2
- **Completed phases:** 1
- **In-progress phase:** None
- **Pending phases:** 2–10
- **Key implemented components:** `Network`, `Node`, `FiberLink`, `Transceiver`, `SwitchingElement`, `INetworkElement`, strong IDs, `Result<T>`/`Error`
- **Current tests/status:** 43 GoogleTest cases, all passing under CTest in Release and Debug (MSVC, warnings-as-errors)
- **Known limitations:** See Phase 1 Summary. No topology/graph, routing, resources, simulation, concurrency, failures, validation framework, or Python layer yet.
- **Next phase:** Phase 2 — Topology & Graph Engine

## Phase 1 Summary

**Status:** Completed

**What was implemented:** The CMake project, the C++20 core domain model, and the unit-test foundation. Nothing belonging to Phase 2+ was implemented.

**Core classes/components** (namespace `opticalnet`, `include/opticalnet/core/`, `src/core/`):
- `StrongId<Tag>` with `NodeId`, `LinkId`, `TransceiverId`, `SwitchingElementId` — explicit construction, ordered, hashable; ids of different kinds cannot be mixed.
- `Error`, `ErrorCode`, `Result<T>` / `Result<void>` — expected failures returned as values; `BadResultAccess` thrown only on misuse.
- `INetworkElement` — common interface (`kind()`, `name()`, `describe()`).
- `Transceiver` — data rate, reach, `canReach()`, `supportsRate()`.
- `SwitchingElement` — ROADM/OXC type, port count, `canSwitch()` (distinct, in-range ports).
- `Node` — name plus references (by id) to one switching element and any number of transceivers; attachments modifiable only through `Network`.
- `FiberLink` — source/target nodes, length, attenuation, channel capacity, `totalLossDb()`. Static properties only.
- `Network` — owns all elements (id-ordered `std::map`s) and enforces relationships: unique ids per kind, links need existing endpoints, equipment attaches to at most one node, referenced objects cannot be removed, nodes with links cannot be removed.

**CMake/build setup:** `CMakeLists.txt` (C++20, no extensions, `opticalnet_core` static library with alias `opticalnet::core`, warnings interface target `/W4 /permissive-` or `-Wall -Wextra -Wpedantic`, warnings-as-errors option on by default, Release default build type), `tests/CMakeLists.txt` (GoogleTest 1.15.2 via `FetchContent`, `gtest_discover_tests`). `nlohmann/json` and pybind11 are intentionally not added yet.

**Tests created** (`tests/unit/`, 43 cases): `Result`/`Error` behavior; strong-id type safety and hashing; transceiver reach/rate boundaries and invalid input (blank, zero, negative, NaN, infinity); switching port-range/loopback rules; node creation, copy independence and common-interface use; link creation, loss calculation, endpoint checks, invalid state (self-loop, bad length/attenuation/capacity); network registration, duplicate-id rejection, missing-endpoint rejection, parallel links, `linksOf`, attachment rules, removal constraints, deterministic iteration order, copy independence.

**Toolchain used:** Visual Studio Build Tools 2022 — MSVC 19.44.35229 (x64), bundled CMake 3.31.6, Ninja 1.13.0. Built with `-G Ninja` after `vcvars64.bat`; compile commands confirmed `-std:c++20`.

**Build status:** Clean configure and build in both Release and Debug, zero warnings (warnings treated as errors).

**Validation results:** `ctest` discovered 43 tests; 43/43 passed in Release and in Debug. Only MSVC was tested; GCC/Clang builds have not been tried.

**Important decisions:**
- Validated static factories (`create`) with private constructors, so invalid elements cannot exist.
- Elements reference each other by id; `Network` is the single owner and validator of relationships.
- Static structure (`Network`) is kept separate from dynamic usage state (Phase 4).
- Directed vs. bidirectional link semantics is deferred to the topology layer (Phase 2); `FiberLink` just has source and target.
- `Result<T>` implemented in-house (C++20 has no `std::expected`).

**Known limitations:**
- No graph/adjacency view, topology loading or validation (Phase 2).
- No routing, allocation, simulation, concurrency, failures, or Python layer.
- `Network` is not thread-safe (Phase 6 treats it as read-only during a run).
- Logging and configuration are not implemented.
- Unit tests only; integration/validation test directories not created yet.
- Not tested with GCC/Clang.

**Commit:** `feat: implement OpticalNet phase 1 foundation` on `main` (the hash cannot be embedded in the commit itself; see `git log`).

**Next phase:** Phase 2 — Topology & Graph Engine

## Phase 2 Summary
Not started

## Phase 3 Summary
Not started

## Phase 4 Summary
Not started

## Phase 5 Summary
Not started

## Phase 6 Summary
Not started

## Phase 7 Summary
Not started

## Phase 8 Summary
Not started

## Phase 9 Summary
Not started

## Phase 10 Summary
Not started

---

When a phase completes, its section is filled with: what was implemented, important classes/components, tests performed, build status, validation results, important decisions, commit hash, remaining limitations, next phase.
