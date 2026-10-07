# OpticalNet — Project Summary (living document)

Updated after each completed phase. Nothing is recorded here as done unless it has actually been implemented and verified.

## Project overview

OpticalNet is an optical network simulation and validation platform that models network topology, routing, resource allocation, concurrent simulation, failures/recovery, and network validation. Core in C++20; Python/FastAPI integration layer. Repository: https://github.com/iamankoo/OpticalNet-Simulator

Source-of-truth documents: [Phases.md](Phases.md), [ARCHITECTURE.md](ARCHITECTURE.md), [TECHSTACK.md](TECHSTACK.md).

## Current status

- **Implementation status:** Phases 1 (core domain model, build/test foundation) and 2 (topology & graph engine) are implemented and tested. Phases 3–10 are not started.
- **Current phase:** Phase 2 complete; ready for Phase 3
- **Completed phases:** 1, 2
- **In-progress phase:** None
- **Pending phases:** 3–10
- **Key implemented components:** `Network`, `Node`, `FiberLink`, `Transceiver`, `SwitchingElement`, `INetworkElement`, strong IDs, `Result<T>`/`Error`, `Topology`, `Adjacency`, `ValidationReport`, `CostMetric`/`linkCost`, JSON topology loader/exporter
- **Current tests/status:** 121 GoogleTest cases (43 Phase 1 + 78 Phase 2), all passing under CTest in Release and Debug (MSVC, warnings-as-errors)
- **Known limitations:** See the Phase 1 and Phase 2 summaries. No routing, resources, simulation, concurrency, failures, validation framework, or Python layer yet.
- **Next phase:** Phase 3 — Routing Engine

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

**Commit:** `d626b8ca537fa5aa7632735866ff5feed7572a54` — `feat: implement OpticalNet phase 1 foundation`.

**Next phase:** Phase 2 — Topology & Graph Engine (completed below)

## Phase 2 Summary

**Status:** Completed

**Objective:** Turn the Phase 1 domain objects into a validated graph that Phase 3 routing can consume directly.

**What was implemented** (`include/opticalnet/topology/`, `src/topology/`, library target `opticalnet::topology`):
- `Topology` — graph over a `Network` (Node = vertex, FiberLink = edge, link cost = weight).
- `Adjacency{neighbor, link}` entries in per-node adjacency lists.
- `ValidationReport` / `ValidationIssue` / `IssueCode` / `Severity`.
- `CostMetric` + `linkCost()`.
- `loadTopologyFromJson`, `loadTopologyFromFile`, `topologyToJson` (`TopologyIo`).
- Sample configs: `configs/ring4.json`, `configs/nsfnet.json` (14 nodes, 21 links; link lengths are approximate).
- Minimal Phase 1 changes: `FiberLink` gained `LinkDirection direction()`, `administrativeCost()` and `allowsTraversal()` (new trailing `create()` parameters are defaulted, so existing callers and tests are unchanged); `ErrorCode::ParseError` added.

**Topology architecture:** `Topology` owns the single `Network` (no duplicate representation) and keeps two adjacency indexes in sync with it. All mutation goes through `Topology`, which delegates validation to `Network` and updates the index only after the `Network` accepts the change; the `Network` is exposed read-only via `network()`. Operations: add/remove node and link (with the Phase 1 rules: nodes with links cannot be removed, links need existing endpoints), equipment add/attach/remove pass-throughs, `hasNode/hasLink/findNode/findLink`, `outgoing`, `incident`, `neighbors`, `linksBetween`, `hasDirectLink`, `degree`, `isReachable`, `connectedComponents`, `isConnected`, `isStronglyConnected`, `validate`.

**Graph representation:** adjacency lists, `unordered_map<NodeId, vector<Adjacency>>`, with one index of edges traversable *from* a node (`outgoing`, what Dijkstra will use) and one of all touching links (`incident`). Each list is sorted by `LinkId`, so iteration is deterministic regardless of insertion order. Reaching a node's list is O(1); insert and removal are O(degree). Lists are returned as `std::span<const Adjacency>` (no copies; invalidated by mutation).

**FiberLink direction decision:** links are **bidirectional by default** and may be explicitly **`Directed`** (source to target only). Why: a physical optical link is normally a fiber pair carrying one direction each, so bidirectional is the natural default, while directed links let later phases model one-way strands or asymmetric failures without a redesign. Representation: `FiberLink::direction()`; a bidirectional link appears in the `outgoing` list of both endpoints, a directed link only at its source; both appear in `incident`. Capacity and cost apply per direction. Phase 3 routing will iterate `outgoing(node)` and never has to interpret direction itself; `FiberLink::allowsTraversal(from, to)` is available for checks.

**Link/cost model:** `FiberLink::administrativeCost` is positive, finite, default 1, validated at creation. `CostMetric` selects the edge weight (`HopCount` = 1, `Distance` = length in km, `Administrative` = the operator cost); `linkCost(link, metric)` reads it. Phase 3 will choose the metric; no path search exists in Phase 2.

**Validation rules:**
- Guaranteed by construction (cannot be violated; tests verify the guarantee): link endpoints exist, ids unique per kind, self-links rejected, non-positive/NaN length, capacity or cost rejected, nodes/equipment cannot be removed while referenced.
- **Parallel links are allowed** deliberately (multiple fiber pairs between two sites; distinct `LinkId`s); duplicate `LinkId`s are rejected.
- Reported by `validate()`: node degree above its switching element's port count (**Error**; each touching link, parallel and directed included, consumes one port); isolated node and disconnected topology (**Warnings**).
- "Valid" (no errors) is deliberately separate from "connected": a disconnected topology is valid but produces warnings; use `isConnected()` / `isStronglyConnected()` for connectivity. An empty topology is valid and counts as connected.

**JSON configuration:** nlohmann/json 3.11.3 (via `FetchContent`, private dependency of `opticalnet_topology`). Schema (documented in `TopologyIo.hpp`): optional `transceivers`, `switching_elements`; required `nodes`; optional `links`. Unsigned-integer ids, optional names, defaults for attenuation (0.2), direction (bidirectional) and cost (1). Strict: unknown keys and wrong types are rejected. Malformed JSON and schema errors return `ErrorCode::ParseError`; domain violations keep their code (`InvalidArgument`, `DuplicateId`, `NotFound`, `ConstraintViolation`); messages name the location (e.g. `links[2]: ...`). Export writes every field explicitly and round-trips exactly. The loader uses a file-local exception internally for early exit and converts it to a `Result` at the API boundary.

**Tests added (78, in `tests/unit/`):** node operations (add, find, duplicate, remove, remove-nonexistent, remove-with-links, id reuse); link operations (valid, unknown endpoint without side effects, self-link, duplicate id, parallel links, remove, remove-nonexistent, directed removal); equipment pass-throughs; construction from an existing `Network`; adjacency (neighbors, `outgoing`/`incident`, ordering independent of insertion order, `linksBetween`, degree); direction semantics (default bidirectional, directed one-way, mixed, opposite directed pair); cost metrics and invalid costs; reachability, components, weak vs. strong connectivity; a randomized 600-step add/remove property test comparing the adjacency index against a brute-force scan of the `Network`; a 5000-node ring; validation (clean, disconnected, isolated, port-count error and boundary, valid is not the same as connected); JSON (valid, defaults, empty, equipment, malformed, missing fields, wrong types, domain errors, duplicates, bad references, unknown keys, round-trip export, sample files). A temporary mutation check (indexing directed links as bidirectional) made 6 tests fail, confirming the direction tests are effective; the mutation was reverted.

**Test count and results:** 121 total (43 Phase 1, unchanged and passing, plus 78 new). Clean build and `ctest`: Debug 121/121 passed, Release 121/121 passed. Zero warnings with warnings-as-errors. C++20 confirmed in compile commands. Toolchain unchanged from Phase 1 (MSVC 19.44.35229, CMake 3.31.6, Ninja 1.13.0). Only MSVC was tested.

**Important design decisions:** single owner of the domain objects (`Topology` over `Network`) rather than a parallel graph structure; structural integrity enforced at the mutation boundary instead of by after-the-fact checks; deterministic ordering everywhere; routing metric choice deferred to Phase 3; `opticalnet_topology` depends on `opticalnet::core`, and JSON is not exposed in public headers.

**Known limitations:**
- No routing or path search (Phase 3); no resource usage tracking (Phase 4).
- Adjacency spans are invalidated by mutation; `Topology` is not thread-safe.
- Failure state (failed links/nodes) is not modeled yet (Phase 7).
- `validate()` checks only port counts, isolation and connectivity; transceiver-reach feasibility belongs to routing (Phase 3).
- `isReachable` runs a fresh BFS per call (no cached reachability).
- Duplicate keys inside one JSON object are resolved by the parser (last wins) and not detected.
- NSFNET link lengths are approximate.
- Not tested with GCC/Clang.

**Commit:** `feat: implement OpticalNet phase 2 topology engine` on `main` (the hash is in `git log`; a commit cannot contain its own hash).

**Next phase:** Phase 3 — Routing Engine

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
