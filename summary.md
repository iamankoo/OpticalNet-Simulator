# OpticalNet — Project Summary (living document)

Updated after each completed phase. Nothing is recorded here as done unless it has actually been implemented and verified.

## Project overview

OpticalNet is an optical network simulation and validation platform that models network topology, routing, resource allocation, concurrent simulation, failures/recovery, and network validation. Core in C++20; Python/FastAPI integration layer. Repository: https://github.com/iamankoo/OpticalNet-Simulator

Source-of-truth documents: [Phases.md](Phases.md), [ARCHITECTURE.md](ARCHITECTURE.md), [TECHSTACK.md](TECHSTACK.md).

## Current status

- **Implementation status:** Phases 1 (core domain model, build/test foundation), 2 (topology & graph engine) and 3 (routing engine) are implemented and tested. Phases 4–10 are not started.
- **Current phase:** Phase 3 complete; ready for Phase 4
- **Completed phases:** 1, 2, 3
- **In-progress phase:** None
- **Pending phases:** 4–10
- **Key implemented components:** `Network`, `Node`, `FiberLink`, `Transceiver`, `SwitchingElement`, `INetworkElement`, strong IDs, `Result<T>`/`Error`, `Topology`, `Adjacency`, `ValidationReport`, `CostMetric`/`linkCost`, JSON topology loader/exporter, `Path`, `RoutingConstraints`, `IRoutingAlgorithm`, `DijkstraRouter`, `RoutingEngine`
- **Current tests/status:** 195 GoogleTest cases (43 Phase 1 + 78 Phase 2 + 74 Phase 3), all passing under CTest in Release and Debug (MSVC, warnings-as-errors)
- **Known limitations:** See the Phase 1–3 summaries. No resource allocation, simulation, concurrency, failures, validation framework, or Python layer yet.
- **Next phase:** Phase 4 — Resource & Connection Management

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
- No routing or path search in this phase (added in Phase 3); no resource usage tracking (Phase 4).
- Adjacency spans are invalidated by mutation; `Topology` is not thread-safe.
- Failure state (failed links/nodes) is not modeled yet (Phase 7).
- `validate()` checks only port counts, isolation and connectivity; transceiver-reach feasibility belongs to routing (Phase 3).
- `isReachable` runs a fresh BFS per call (no cached reachability).
- Duplicate keys inside one JSON object are resolved by the parser (last wins) and not detected.
- NSFNET link lengths are approximate.
- Not tested with GCC/Clang.

**Commit:** `fd640f5ec77f5aced2e9cc441c4dd727391d8a71` — `feat: implement OpticalNet phase 2 topology engine`.

**Next phase:** Phase 3 — Routing Engine (completed below)

## Phase 3 Summary

**Status:** Completed

**Objective:** Given a source, destination, topology and routing constraints, find the cheapest feasible path and return it with its cost, ready for Phase 4 resource allocation.

**Routing architecture** (`include/opticalnet/routing/`, `src/routing/`, library target `opticalnet::routing`, depends on `opticalnet::topology`):
- `IRoutingAlgorithm` — one-method interface: `findPath(topology, source, destination, constraints) -> Result<Path>`.
- `DijkstraRouter` — the implementation.
- `RoutingEngine` — thin entry point holding an algorithm (Dijkstra by default). It never owns the topology: the caller passes the `Topology` on every call. It finds paths only and reserves nothing.
- `Path` and `RoutingConstraints` — the result and request types.
- Flow: `Topology -> RoutingEngine -> Path -> (Phase 4) ResourceManager`. Routing owns no resource state; there is no second copy of the graph.

**Dijkstra implementation:** binary min-heap (`std::priority_queue`) over `Topology::outgoing()`, O((V + E) log V). No full-network scan per node; labels live in an `unordered_map`, so per-query set-up is proportional to the part of the graph actually explored. Predecessor links are kept per label and the path is rebuilt source-to-destination at the end. All edge costs are positive (guaranteed by `FiberLink` validation).

**Path representation:** immutable `Path` holding the ordered node sequence, the ordered **link** sequence (essential with parallel links), total cost, the `CostMetric` the cost is expressed in, and hop count. Invariant `nodes = links + 1`, enforced by `Path::create` (returns `Result`). Trivial path = one node, no links, cost 0.

**Cost metrics:** `RoutingConstraints::metric` selects `HopCount`, `Distance` or `Administrative` (default) through `linkCost()`. Tests use graphs where each metric picks a different route.

**Constraints** (`RoutingConstraints`; all take part in the search instead of being checked afterwards):
- `maxCost` — in the units of the chosen metric; partial paths exceeding it are never extended (tested: a 2000-node chain with `maxCost` 5 inspects fewer than 40 edges). Optical reach is expressed as `{Distance, maxCost = transceiver.reachKm()}`.
- `maxHops` — **exact**. A plain Dijkstra run ignores it; only if its cheapest path has too many hops is the search repeated over (node, hops-used) states, which finds the cheapest path within the limit (a costlier shorter path is found when needed). Worst case O(maxHops·(V+E) log V), paid only when the limit binds.
- `minLinkCapacityChannels` — links whose configured capacity is below the value are not used. This is *static* capacity only (`FiberLink::capacityChannels`).
- `blockedNodes`, `blockedLinks` — never used; a blocked source or destination makes the request infeasible (including source == destination).
- `linkFilter` — optional per-link predicate. This is the **Phase 4 seam**: the resource manager can supply "link has free capacity" without routing owning or copying resource state. Dynamic capacity is not implemented here.
- Not implemented: allowed-lists (blocked-lists cover the need), and any second additive resource (e.g. a distance limit while minimising another metric), which would make the problem a constrained shortest-path problem (NP-hard in general).

**Directed-link behavior:** only `outgoing()` edges are followed, so a directed link is used source → target only and a bidirectional link both ways. Tested: forward allowed, reverse blocked, reverse via a second link, a directed edge forcing a detour, mixed topologies.

**Parallel-link behavior:** each parallel link is its own edge; the cheapest under the metric is chosen, and the returned `Path` names the exact `LinkId`. Parallel links of different direction are evaluated independently.

**Deterministic tie-breaking:** among routes to the same node: lower cost, then fewer hops, then smaller predecessor `NodeId`, then smaller `LinkId` (the last follows from ascending-`LinkId` adjacency order). Heap pops follow (cost, hops, NodeId). Results are independent of link insertion order (tested with six rotations) and identical across repeated runs. Costs are compared as exact doubles, so equal-cost ties are guaranteed only for costs that sum exactly (e.g. integers).

**Error handling:** all failures are `Result` errors, none throw. `NotFound` (unknown source or destination), `InvalidArgument` (negative, NaN or infinite `maxCost`), `NoRoute` (destination unreachable even without constraints) and `NoFeasibleRoute` (a route exists but none satisfies the constraints; found by one extra unconstrained search, only on failure). `source == destination` returns the trivial path (an unknown node is still `NotFound`). A null algorithm passed to `RoutingEngine` is a programming error and throws `std::invalid_argument`. New `ErrorCode`s: `NoRoute`, `NoFeasibleRoute`.

**Tests (74 new, in `tests/unit/`):** `Path` (5 cases: shape, trivial path, invalid shape/cost, equality incl. parallel links); basic routing (one hop, shortest of two, cheaper-but-longer, reconstruction order, source == destination, unreachable, unknown endpoints, topology untouched); metrics (each metric chooses a different route); directed links; parallel links; determinism (equal-cost branches, fewer hops, insertion-order independence, repeated runs on a grid, plus two cases written specifically so the hop and predecessor tie-break rules decide the outcome); constraints (max cost at/below boundary, max hops at/below boundary, hop limits that bind, combined constraints, static capacity, blocked nodes/links, protection-path use, link filter, error-code distinction); the engine (custom algorithm, null algorithm); a randomized test comparing the router with **exhaustive enumeration of all simple paths** on 5000 random 9-node multigraphs with directed, bidirectional and parallel links under random metrics and constraint combinations (it asserts that every outcome occurred, including >40 cases where the hop limit forced a costlier path); large-graph tests (below).

**Mutation checks performed:** I temporarily broke the code and confirmed the tests fail: disabling the hop-limit re-search (9 failures), removing maxCost pruning (8), dropping the fewer-hops preference (1), dropping the predecessor tie-break (1). The first versions of the two tie-break tests did *not* detect those last two mutations; they were strengthened until they did. A fifth mutation (dropping the link-id tie-break) survived because that comparison is dead code given ascending adjacency order, so it was removed and documented instead.

**Test count and results:** 195 total = 43 Phase 1 + 78 Phase 2 + 74 Phase 3 (all Phase 1 and 2 tests unchanged). Clean rebuild and `ctest`: Debug 195/195 passed (~16 s), Release 195/195 passed (~5 s). Zero warnings under warnings-as-errors. C++20 confirmed in the compile commands. Toolchain unchanged (MSVC 19.44.35229, CMake 3.31.6, Ninja 1.13.0); only MSVC was tested.

**Large-graph tests:** 5000-node linear chain routed in both directions (4999 hops, exact cost, under a 10 s sanity bound); 5000-node ring (shorter side chosen, 999 and 1001 hops; equal-cost opposite node resolved deterministically); 70×70 grid (4900 nodes) shortest path with exact Manhattan cost, plus a binding hop limit that is feasible at the exact minimum and infeasible one below; 400-node chain with an express link under a binding hop limit. These are correctness/sanity tests, not benchmarks (Phase 10).

**Important design decisions:** routing is a pure read of `Topology` and holds no state; constraints prune during the search; exact hop limits via a lazily-run layered search instead of an approximation; reach is a distance limit rather than a second constraint; static capacity only, with a filter hook for Phase 4; a small interface (`IRoutingAlgorithm`) kept because `ARCHITECTURE.md` plans pluggable algorithms, with no deeper hierarchy.

**Known limitations:**
- Single-path, single-criterion: no k-shortest paths, no disjoint-pair routing (blocked-link sets allow it to be composed by the caller), no multi-constraint additive limits beyond cost and hops.
- Capacity feasibility uses configured capacity only; free capacity requires Phase 4 and the `linkFilter`.
- The hop-limited search can be slow for very large graphs with a large binding `maxHops`.
- Ties between sums that are equal only up to floating-point rounding are not guaranteed to resolve identically.
- No transceiver or switching-element feasibility beyond the distance limit and port counts from Phase 2.
- Not tested with GCC/Clang.

**Commit:** `feat: implement OpticalNet phase 3 routing engine` on `main` (the hash is in `git log`; a commit cannot contain its own hash).

**Next phase:** Phase 4 — Resource & Connection Management

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
