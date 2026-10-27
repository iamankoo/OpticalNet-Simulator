# OpticalNet — Project Summary (living document)

Updated after each completed phase. Nothing is recorded here as done unless it has actually been implemented and verified.

## Project overview

OpticalNet is an optical network simulation and validation platform that models network topology, routing, resource allocation, concurrent simulation, failures/recovery, and network validation. Core in C++20; Python/FastAPI integration layer. Repository: https://github.com/iamankoo/OpticalNet-Simulator

Source-of-truth documents: [Phases.md](Phases.md), [ARCHITECTURE.md](ARCHITECTURE.md), [TECHSTACK.md](TECHSTACK.md).

## Current status

- **Implementation status:** Phases 1 (core domain model, build/test foundation), 2 (topology & graph engine), 3 (routing engine), 4 (resource & connection management), 5 (simulation engine), 6 (multithreaded simulation), 7 (failure & recovery simulation) 8 (validation & testing framework), 9 (Python + FastAPI integration) and 10 (optimization, benchmarking & release) are implemented and tested. **OpticalNet — All 10 Phases COMPLETED.**
- **Current phase:** None — all phases complete
- **Completed phases:** 1, 2, 3, 4, 5, 6, 7, 8, 9, 10
- **In-progress phase:** None
- **Remaining phases:** None
- **Key implemented components:** `Network`, `Node`, `FiberLink`, `Transceiver`, `SwitchingElement`, `INetworkElement`, strong IDs, `Result<T>`/`Error`, `Topology`, `Adjacency`, `ValidationReport`, `CostMetric`/`linkCost`, JSON topology loader/exporter, `Path`, `RoutingConstraints`, `IRoutingAlgorithm`, `DijkstraRouter`, `RoutingEngine`, `ConnectionRequest`, `Connection`, `ConnectionManager`, `ResourceManager`, `LinkUsage`, `SimulationEngine`, `SimulationConfig`, `RequestGenerator`, `EventQueue`, `Rng`, `SimulationResult`/`SimulationMetrics`, `ThreadPool`, `SimulationStudy`/`StudyConfig`/`StudyResult`/`StudyAggregate`, `SampleStatistics`, `FailureState`, `RecoveryPolicy`, `ScriptedFailure`/`RandomFailureConfig`, `generateRandomFailures`, validation layer (`ValidationResult`, invariant checkers, `exhaustiveRoute`, `ShadowLedger`, deterministic generators), pybind11 module `opticalnet._opticalnet`, Python package `opticalnet`, FastAPI app (`api/`)
- **Current tests/status:** 606 GoogleTest cases (43 Phase 1 + 78 Phase 2 + 74 Phase 3 + 59 Phase 4 + 74 Phase 5 + 62 Phase 6 + 101 Phase 7 + 105 Phase 8 + 10 Phase 10; 501 unit + 105 validation) plus 102 Python/API pytest cases (binding + HTTP), run by CTest as `python_api_tests` = 607 CTest entries; all passing in Debug and Release (GCC 13.3 on Linux, warnings-as-errors; Phases 1-9 also on MSVC)
- **Known limitations:** See the Phase 1–10 summaries.
- **Next phase:** None. The project is complete.

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

**FiberLink direction decision:** links are **bidirectional by default** and may be explicitly **`Directed`** (source to target only). Why: a physical optical link is normally a fiber pair carrying one direction each, so bidirectional is the natural default, while directed links let later phases model one-way strands or asymmetric failures without a redesign. Representation: `FiberLink::direction()`; a bidirectional link appears in the `outgoing` list of both endpoints, a directed link only at its source; both appear in `incident`. Cost applies to each traversal; capacity is one pool per link shared by both directions (clarified in Phase 4). Phase 3 routing will iterate `outgoing(node)` and never has to interpret direction itself; `FiberLink::allowsTraversal(from, to)` is available for checks.

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

**Commit:** `18a36297d2fe00333bd81d7be56703c57b4ffcb4` — `feat: implement OpticalNet phase 3 routing engine`.

**Next phase:** Phase 4 — Resource & Connection Management (completed below)

## Phase 4 Summary

**Status:** Completed

**Objective:** Turn "a feasible path exists" (Phase 3) into "resources can actually be reserved for this connection and returned correctly later": dynamic capacity state, atomic allocation and release, and a connection lifecycle.

**What was implemented** (`include/opticalnet/resources/`, `src/resources/`, library target `opticalnet::resources`, depends on `opticalnet::routing`):
- `ConnectionId` (a `StrongId`, added to `Ids.hpp`).
- `ConnectionRequest` — id, source, destination, `capacityChannels`, `RoutingConstraints`; `validate()`.
- `LinkUsage{total, allocated, available}`.
- `ResourceManager` — dynamic per-link allocation state.
- `Connection` — an immutable record of an established connection.
- `ConnectionManager` — establish / release / registry.
- New `ErrorCode`s `InsufficientCapacity` and `InconsistentState`; new `IssueCode`s `AllocationExceedsCapacity`, `AllocationOnUnknownLink`, `ConnectionAllocationMismatch` (reusing the Phase 2 `ValidationReport`).

**Separation of responsibilities:**
```text
Topology           static structure (incl. each link's total capacity)   referenced, never owned by the others
RoutingEngine      path calculation, stateless
ResourceManager    dynamic state: channels allocated per link
ConnectionManager  connection lifecycle; owns a ResourceManager and a RoutingEngine
```

**Resource model:** capacity is counted in **channels per link**; a connection of demand N uses N channels on every link of its path. A link's channels form **one pool shared by both directions** of a bidirectional link (conservative; Phase 2's earlier "per direction" remark was corrected). Wavelength/spectrum assignment is not modelled. `ResourceManager` stores only `allocated` counts (`map<LinkId, uint32>`, entries removed at zero); `total` is read from the `Topology` on demand, so static capacity exists in exactly one place, and `available = total - allocated` is derived, never stored. Rules enforced: demand > 0, `allocated <= total`, `available` never negative (counters are unsigned and every subtraction is pre-checked; `usage()` clamps `available` to 0 if a topology change ever leaves `allocated > total`), no release beyond what is allocated.

**ResourceManager:** `usage`, `hasCapacity`, `allocated`, `allocations`, `usages`; network-wide `totalCapacity/totalAllocated/totalAvailable`; `canAllocate`, `allocate`, `release`; `validate()`. It references the topology (which must outlive it) and does not own it.

**Atomicity strategy:** two-step allocate/release. The request is first merged into per-link needs (a link repeated in the argument counts repeatedly), every link is validated (exists, enough free channels / enough allocated), and only then are counters changed. The mutation step cannot fail, so every call is all-or-nothing and no rollback is needed. `ConnectionManager` allocates *before* registering a connection, so a failure leaves neither capacity nor registry changed.

**Routing/resource integration:** `establish()` copies the request's constraints and installs a composed `linkFilter`: "link has `demand` free channels" AND the caller's own filter. Routing therefore avoids full links through the Phase 3 seam without holding any resource state, and no new routing algorithm exists. If routing then fails with `NoFeasibleRoute`, one extra search without the capacity filter distinguishes `InsufficientCapacity` (capacity is the only obstacle) from a genuine `NoFeasibleRoute`/`NoRoute`.

**Connection model:** `Connection` keeps the request, the exact `Path`, and the demand, so release never recomputes a route (the topology may have changed; parallel links make node sequences ambiguous). Created only by `ConnectionManager`.

**ConnectionManager:** `establish(request)`; `establishOnPath(request, path)` for a caller-supplied path (checks endpoints, existence of every link, contiguity and link direction); `release(id)`; `find`, `contains`, `activeCount`, `activeConnections()` (read-only `map` ordered by id); `resources()`; `validate()` (resource checks plus a cross-check that per-link allocation equals what the active connections hold). Duplicate ids are rejected; a request with `source == destination` is rejected (a connection needs at least one link). No automatic expiry: connections live until `release`.

**Release behavior:** releases exactly the stored links and demand, then removes the connection. A second release is `NotFound` and changes nothing. Release still works if a link has since been removed from the topology (it heals the state; `validate()` reports the stale allocation until then). If bookkeeping were ever inconsistent, `release` returns `InconsistentState` and keeps the connection registered.

**Capacity accounting:** per link (`LinkUsage`) and network-wide totals, as above.

**Error handling:** all via `Result`, none throw. `InvalidArgument` (zero capacity, `source == destination`, empty path, path not matching the request or not walkable), `NotFound` (unknown node, link or connection), `DuplicateId`, `NoRoute`, `NoFeasibleRoute`, `InsufficientCapacity`, `InconsistentState`. Negative capacity cannot be expressed (unsigned type, checked by a `static_assert`).

**Concurrency:** the managers are **single-threaded**; no mutexes were added. All mutable state sits behind `ResourceManager`/`ConnectionManager` methods, and `ConnectionManager` owns its `ResourceManager`, so Phase 6 can add one lock around the manager (or finer locking inside) without changing the API.

**Tests (59 new, `ResourceManagerTests.cpp`, `ConnectionManagerTests.cpp`):**
- *Allocation/accounting:* single- and multi-link allocation, exact-capacity boundary, overflow-safe huge demands, three connections filling a link (30+40+30) then rejecting a fourth, `allocated + available == total` on every link and in the totals after every operation, shared pool for both directions.
- *Atomicity (mandatory):* an early link has room but the last link does not → every link unchanged; same for failure on the first link, an unknown link mid-path, a repeated link in one call, a failed (over-)release, and an explicit path whose second link is full.
- *Release:* exact restore, partial release, reuse (allocate → release → allocate), over-release, double release, unknown connection.
- *Routing integration:* shortest route lacks capacity → alternative route chosen; shortest route returns after release; every route full → `InsufficientCapacity`; caller filter combined with the capacity filter; two managers over one topology are independent.
- *Parallel links:* the exact chosen `LinkId` is allocated and released, the parallel link is untouched.
- *Registry/failures:* insert, lookup, ordered iteration, duplicates (state unchanged), missing connection, invalid requests (zero capacity, same endpoints, unknown nodes), invalid explicit paths (trivial, wrong endpoints, unknown link, non-contiguous, directed link backwards), release uses stored links even after the topology gained a cheaper link or lost the allocated one.
- *Consistency:* `validate()` clean after normal use; detects allocation on a removed link and allocation above capacity after a link is replaced with a smaller one.
- *Large/stress:* (1) 20,000 random allocate/release operations over 2000 links checked against an independent shadow ledger (accepted > 3000, rejected > 500 so capacity limits are really hit); (2) a 20×20 grid (400 nodes, 760 links, 8 channels each) with 4000 random establish/release operations: after the run the total allocation equals Σ demand × hops of the active connections, no link exceeds capacity, `validate()` is clean throughout, and releasing everything drains the network to zero.

**Mutation checks performed:** partial allocation (checking and mutating link by link) was caught by 4 tests; partial release by 1; removing the capacity filter from routing by 5. Sources were restored and verified identical afterwards.

**Test count and results:** 254 total = 43 Phase 1 + 78 Phase 2 + 74 Phase 3 + 59 Phase 4 (earlier tests unchanged). Clean rebuild and `ctest`: Debug and Release both 254/254 passed. Zero warnings under warnings-as-errors. C++20 confirmed in the compile commands. Toolchain unchanged (MSVC 19.44.35229, CMake 3.31.6, Ninja 1.13.0); only MSVC was tested.

**Important design decisions:** dynamic state stored apart from the topology, which keeps one copy of static capacity; allocate-after-check rather than rollback; routing coupled to resources only through `linkFilter`; the `Connection` stores its own links and demand; one owner (`ConnectionManager`) for the whole lifecycle so a single lock suffices later; `establishOnPath` kept because Phase 7 needs to place precomputed backup paths.

**Known limitations:**
- Single-threaded: a `ConnectionManager`/`ResourceManager` must not be shared between threads (Phase 6 gives each concurrent replication its own).
- Capacity is a plain channel count: no wavelength identity, continuity or fragmentation, and one pool per link regardless of direction.
- `ResourceManager` and `ConnectionManager` hold pointers to the `Topology`, which must outlive them. Topology edits while connections exist are tolerated but can leave stale allocations (reported by `validate()`, healed by release).
- No connection lifetime, expiry, scheduling or blocking statistics in this phase (added in Phase 5); no re-routing of active connections (Phase 7).
- `totalCapacity()` and `totalAvailable()` scan all links on each call.
- Not tested with GCC/Clang.

**Commit:** `953bb11801747f91dd360c37f3b296fe2bead185` — `feat: implement OpticalNet phase 4 resource management`.

**Next phase:** Phase 5 — Simulation Engine (completed below)

## Phase 5 Summary

**Status:** Completed

**Objective:** Turn manual connection establishment into a discrete-event simulation of connection requests arriving and expiring over virtual time, with reproducible runs and meaningful metrics.

**Simulation architecture** (`include/opticalnet/simulation/`, `src/simulation/`, library target `opticalnet::simulation`, depends on `opticalnet::resources`):
```text
SimulationConfig ─► SimulationEngine ─┬─ EventQueue
                                      ├─ RequestGenerator (uses Rng)
                                      ├─ ConnectionManager (routing + resources, Phase 3/4)
                                      └─ metrics / trace  ─►  SimulationResult
```
- `SimulationEngine::run(topology, config)` runs a generated workload; `runScript(topology, arrivals, config)` runs an explicit list of `ScriptedArrival`s (used for hand-computed scenarios). Both share one event loop.
- The engine is stateless: each run builds its own `ConnectionManager`; the `Topology` is only read (no active-connection state is ever written to it), and runs are independent and repeatable.
- Single-threaded by design. **Phase 5 simulation is intentionally single-threaded; Phase 6 introduces concurrent simulation workers.** No mutexes or threads were added.

**Discrete-event model and simulation clock:** the clock (`SimTime`, a `double` in abstract time units) jumps straight from one event to the next. Nothing sleeps, waits or reads a wall clock; a run over 10^9 time units costs only the processing of its events (tested). The only events are `ConnectionArrival` and `ConnectionRelease`.

**Event model and ordering:** `SimulationEvent{time, type, connection, sequence}` in an `EventQueue` (`std::priority_queue`). Deterministic total order: (1) earlier time first; (2) at equal times by type: **releases before arrivals**, so capacity freed at `t` is usable by a request arriving at `t`; (3) at equal time and type, by sequence number (scheduling order). Events with negative/NaN/infinite times are rejected.

**Request generation** (`RequestGenerator`, testable on its own): produces `GeneratedRequest{arrival, ConnectionRequest, lifetime}`. Ids are 1, 2, 3, ... in generation order. Source is uniform over the topology's nodes and the destination uniform over the *other* nodes, so **source ≠ destination always; self-connections are never generated** (a Phase 4 connection needs at least one link). Demand is uniform in `[minCapacityChannels, maxCapacityChannels]`. `SimulationConfig::routing` (metric, hop/cost limits, blocked elements) is copied into every request. Draw order per request: gap, source, destination, capacity, lifetime. Every random quantity is drawn at generation time, so **the workload does not depend on the network's reaction**: the same seed gives the same requests on a roomy and a congested network (tested).

**Arrival distribution:** `arrivalRate` λ ≥ 0 requests per time unit. `Timing::Exponential`: inter-arrival gaps are exponential with mean 1/λ (a Poisson process of rate λ). `Timing::Fixed`: a request exactly every 1/λ. λ = 0 means no requests. Arrival times are strictly increasing (a zero or absorbed gap is bumped to the next representable time).

**Lifetime distribution:** `meanLifetime` > 0; `Timing::Exponential` (mean) or `Timing::Fixed` (exact). An accepted connection's release event is scheduled at `arrival + lifetime` (clamped to the largest finite double so absurd values cannot overflow).

**RNG/seed behavior:** `std::mt19937_64` seeded from `SimulationConfig::seed`, with `std::exponential_distribution` and `std::uniform_int_distribution` behind a small `Rng` wrapper. No `rand()`. The same seed gives the same sequence for a given compiler and standard library; the standard fixes the engine but not the distributions' algorithms, so exact random values may differ between standard-library implementations. Tests therefore check reproducibility and ranges, never hard-coded random numbers.

**Simulation termination:** events are processed in order while `time <= endTime`; arrivals are generated only up to `endTime` (and `maxRequests` if set). Releases beyond `endTime` are not processed (those connections are still active at the end). `maxRequests`, if set, caps the number of generated requests; if the cap is reached and every scheduled release occurred before `endTime`, the run ends at its last event (`duration` = that time), otherwise at `endTime`. Zero duration, zero rate, zero cap, empty or single-node topologies all return valid results. A scripted run ignores arrivals after `endTime`.

**Processing:** *Arrival* → `ConnectionManager::establish`; success schedules the release and records path hops/cost; failure records the `ErrorCode` as the rejection reason. *Release* → `ConnectionManager::release` of exactly the stored connection (never rerouted). No allocation logic exists in the simulator itself. At the end the engine checks that `ConnectionManager::validate()` is clean and that its own incremental channel/connection counts equal the `ResourceManager`'s; a mismatch is returned as `InconsistentState`.

**Metrics (`SimulationMetrics`, plain comparable data, no dependency on the API layer).** With R = total requests, a connection of demand d over h links holds d·h channel-links, and capacity = sum of every link's configured channels:
```text
acceptanceRate        = acceptedRequests / R                         (0 if R = 0)
blockingRate          = rejectedRequests / R                         (0 if R = 0)
averageUtilization    = ∫ allocated(t) dt over [0, duration]  /  (totalCapacityChannels · duration)   (0 if either factor is 0)
                        — a TIME-weighted average (rectangles between events), not an average over events
peakAllocatedChannels = max allocated(t), observed after each processed event
averagePathCost, averageHopCount = mean over accepted requests (0 if none); cost is in each request's routing metric
maxHopCount, peakActiveConnections, activeConnectionsAtEnd, totalReleases, eventsProcessed
finalAllocatedChannels, finalAvailableChannels, totalCapacityChannels, rejectionsByReason (per ErrorCode), duration
```
`SimulationResult` also holds the seed and an optional per-event `trace` (`recordTrace`, off by default) with time, type, connection, endpoints, demand, success, rejection reason and hops.

**Determinism:** same topology + config + seed gives an `==` result: identical request sequence, event order, accept/reject outcomes, trace and metrics (tested by comparing the full traces and metrics of two runs). Different seeds change the workload (asserted on the generated arrival times/endpoints, not that every seed always differs).

**Tests (74 new, in `SimulationPrimitivesTests.cpp`, `RequestGeneratorTests.cpp`, `SimulationEngineTests.cpp`):**
- *RNG (6):* same seed → same sequence, different seeds differ, ranges, full coverage of an integer range, exponential sample mean, precondition errors.
- *Events (8) and config (3):* chronological order, equal-time ordering (releases before arrivals), sequence tie-break independent of connection id, sequence numbers, time beats type, far-future event, invalid times, empty queue; config defaults, invalid values, boundaries.
- *Request generator (18):* valid distinct endpoints (self-connections never generated), every node used, capacity bounds and coverage, sequential ids, strictly increasing arrivals, constraints copied, same seed → same workload, different seed → different workload, workload independent of links/capacity, cap, horizon, zero rate/duration/cap, fewer than two nodes, invalid config, exact `Fixed` timing, exponential means (20,000 samples).
- *Hand-computed fixtures (3):* (a) scripted A–B–C line with 4 requests: exact counts, rejection reason, peak 16, utilization 0.41 (integral 82 / (20·10)), hop/cost averages 4/3, releases, active count, and the exact event order including the same-time release-before-arrival; (b) a generated run with fixed timings on a 2-node link: 10 requests, accepted #1,#2,#4,#5,#7,#8,#10, rejected #3,#6,#9, utilization 0.58 (integral 58 / 100), 5 releases, 15 events.
- *Scripted and generated edge cases (18 + 10):* idle network, one accepted, one rejected, release timing, `NoFeasibleRoute`, `NoRoute`, unknown node and duplicate id (rejected, nothing allocated), resource reuse after release at the same instant, same-time arrivals in script order, arrivals after/at the horizon, very short lifetimes, 4-billion-channel links (no overflow), overflowing lifetime, invalid scripts, zero duration, trace on/off, repeatability; empty topology, single node, zero rate, zero duration, zero cap, invalid config, no links, cap-ended vs. horizon-ended runs, virtual time (10^9 units).
- *Determinism (4):* same seed → identical full result, different seeds differ, one engine object reused, workload identical across networks of different capacity.
- *Congestion and large runs (4):* a saturated 3×3 grid (blocking > 0.5, utilization > 0.5), a light load with zero blocking, routing configuration applied (hop limit 1), and a **10×10 grid (100 nodes, 180 links) with ~5000 requests and ~10,000 events** whose invariants are all checked: accepted + rejected = total, rejection reasons sum to the rejections, events = requests + releases, active = accepted − releases, allocated + available = capacity, and the final state recomputed independently from the trace matches the engine's.

**Mutation checks performed:** swapping the same-time priority (4 tests failed), integrating utilization incorrectly (3), and replacing the sequence tie-break with connection id (2) were all caught; sources restored and verified identical.

**Test count and results:** 328 total = 43 Phase 1 + 78 Phase 2 + 74 Phase 3 + 59 Phase 4 + 74 Phase 5 (earlier tests unchanged). Clean rebuild and `ctest`: Debug 328/328 passed, Release 328/328 passed. Zero warnings under warnings-as-errors. C++20 confirmed in the compile commands. Toolchain unchanged (MSVC 19.44.35229, CMake 3.31.6, Ninja 1.13.0); only MSVC was tested.

**Important design decisions:** workload generation is separate from, and independent of, network state; an explicit-script entry point next to the generator so exact scenarios can be verified by hand; releases ordered before arrivals at equal times; time-weighted utilization; a stateless engine with per-run state; reasons for rejection kept per `ErrorCode`; the engine cross-checks its bookkeeping against the resource layer at the end of every run.

**Known limitations:**
- Each run is single-threaded (Phase 6 runs independent replications concurrently, see below); no failure/recovery events (Phase 7); no benchmarking (Phase 10).
- Only Poisson (or fixed) arrivals and exponential (or fixed) lifetimes, uniform endpoint selection and uniform demand: no traffic matrices, hotspots or other distributions.
- No warm-up period: metrics include the initial transient from an empty network.
- No confidence intervals or multi-run aggregation.
- Utilization counts channel-links over total channels, so long routes weigh more; it is not a per-link average.
- Exact random sequences are implementation-specific across standard libraries.
- Times are `double`, so equal-time ordering relies on exactly equal values (e.g. fixed timings that are exactly representable).
- Not tested with GCC/Clang.

**Commit:** `3eccace8b94dba863dbc5061fd50658f6a9f390a` — `feat: implement OpticalNet phase 5 simulation engine`.

**Next phase:** Phase 6 — Multithreaded Simulation (completed below)

## Phase 6 Summary

**Status:** Completed

> **Phase 6 parallelizes independent simulation replications. Individual simulation event loops remain single-threaded.**

**Objective:** Run many independent Phase 5 simulations concurrently (a *simulation study*) with results that are reproducible and independent of the number of threads or of scheduling.

**Multithreaded architecture** (`include/opticalnet/concurrency/`, `src/concurrency/`, library `opticalnet::concurrency`; `SimulationStudy` and `Statistics` live in `opticalnet::simulation`, which now depends on the concurrency library; threads come from `Threads::Threads`):
```text
                       Simulation study (StudyConfig)
                                   │
                           study coordinator  ── derives one seed per replication (on the coordinating thread)
                                   │
                              ThreadPool ───────────────┬────────────────┬──────────────┐
                                                        ▼                ▼              ▼
                                                replication 0      replication 1   replication N-1
                                                (own Rng, EventQueue, ConnectionManager,
                                                 ResourceManager, metrics — nothing shared)
                                                        └────────────────┴──────────────┘
                                                          results collected by index → StudyResult
```
Unchanged: `SimulationEngine::run`, `runScript` and every Phase 1–5 class (apart from new error/code additions, see below). No topology locks and no shared mutable simulation state were added.

**Worker pool (`ThreadPool`):** standard C++ only (`std::thread`, `std::mutex`, `std::condition_variable`, `std::queue`, `std::packaged_task`/`std::future`). `submit(f)` returns a `std::future` carrying the result *or the exception* of the task (an exception never escapes into a worker and is never swallowed). Tasks start in FIFO order. `shutdown()` (also run by the destructor) stops accepting tasks, lets the workers **finish every task already queued**, and joins every thread; it is idempotent and safe to call concurrently. No thread is detached or outlives the pool. Constructing with zero workers, `submit` after shutdown, and `shutdown` from inside a worker (which would self-join) are programming errors and throw. If thread creation fails part-way, the started threads are joined before the exception propagates.

**Simulation study (`SimulationStudy`, `StudyConfig`, `StudyResult`):**
- `SimulationStudy::run(topology, config, studyConfig)` runs `studyConfig.replications` replications with `studyConfig.baseSeed`; `SimulationConfig::seed` is ignored.
- `SimulationStudy::runReplications(studyConfig, fn)` is the same coordinator with an injectable per-replication function (also the seam used by the tests to prove concurrency and to check aggregation exactly).
- **Worker count:** `workerCount = 0` means `std::thread::hardware_concurrency()` (falling back to 1 if the platform reports 0); explicit values are used as given; the pool never gets more threads than there are replications (`resolveWorkerCount`, `StudyResult::workersUsed`).
- **Replications:** `replications` must be ≥ 1 (**zero is rejected as `InvalidArgument`**, so no meaningless aggregates exist) and ≤ 1,000,000.

**Replication seeds:** replication *i* uses `splitmix64(baseSeed + (i + 1) · 0x9E3779B97F4A7C15)`, computed on the coordinating thread. It depends only on (baseSeed, i) — never on the worker or timing — and is **injective in i** (odd multiplier and the splitmix64 mixer are both bijections modulo 2^64), so no two replications of a study can share a seed. Different base seeds give different sequences. No RNG is ever shared between threads (no mutex-guarded global RNG).

**Determinism guarantees:** for the same topology, simulation config, base seed and replication count, replication *i* produces the identical `SimulationResult` (full trace and metrics) whether run with 1, 2, 4, 8, automatic, or far more workers than replications, and it equals a standalone `SimulationEngine::run` with the derived seed. Results are returned **by replication index, never by completion order**, and aggregates are computed afterwards in index order.

**Thread ownership:**
| Object | Ownership |
|---|---|
| `Topology` | shared, immutable, read concurrently (the caller must not mutate it during a study) |
| `SimulationConfig`, `StudyConfig` | shared read-only; each task copies the config and sets its own seed |
| `SimulationEngine` (stateless), `Rng`, `EventQueue`, `RequestGenerator`, `ConnectionManager`, `ResourceManager`, metrics | one set per replication, created and destroyed inside its task |
| `SimulationResult` | produced by one task, handed to the coordinator through a future |
| `StudyResult` | built only by the coordinating thread |

An audit of Phases 1–5 found no mutable global/static state (the only `static` is a function-local `const` in the JSON loader). Topology reads are `const` lookups in standard containers, safe concurrently. A `RoutingConstraints::linkFilter` inside the config is invoked from several threads and must be thread-safe.

**Aggregation (`StudyAggregate`, `aggregate()`)**, all computed from the ordered replications; ratios come from totals, not from averaging ratios:
```text
totalRequests, totalAccepted, totalRejected, totalReleases, rejectionsByReason   sums over replications
aggregateAcceptanceRate = totalAccepted / totalRequests        aggregateBlockingRate = totalRejected / totalRequests   (0 if no requests)
meanActiveConnectionsAtEnd, meanPeakActiveConnections, meanPeakAllocatedChannels,
meanFinalAllocatedChannels, meanAverageUtilization             equal-weight means over replications
averagePathCost, averageHopCount   = Σ(replication mean × accepted) / totalAccepted   (accepted-weighted; 0 if none accepted)
acceptanceRate, blockingRate, averageUtilization   SampleStatistics of the per-replication values
```
`SampleStatistics`: count, mean, sample standard deviation (n−1), standard error, and a 95% confidence half-width using Student's *t* (exact table for 1–30 degrees of freedom, first-order approximation above). The interval assumes independent, roughly normal per-replication values; with fewer than 2 replications the spread is reported as 0 meaning "unknown". Replications with zero requests contribute a rate of 0 to those distributions. No statistics dependency was added.

**Error handling:** `InvalidArgument` for a bad `StudyConfig` or `SimulationConfig` (before any thread starts). If a replication returns an error, the **whole study fails** with that error (message prefixed `replication <i>: `), choosing the lowest failing index so the reported error is the same for every worker count; the other replications still run to completion and every worker is joined before the call returns (nothing is cancelled, nothing left running). An exception thrown inside a replication (or a failure to create threads or schedule work) becomes `InternalError` naming the replication and the exception text; nothing is swallowed. Added `ErrorCode::InternalError`.

**Shutdown semantics:** the study always `shutdown()`s its pool (draining the queue and joining all workers) before returning, on success and on every failure path; the pool destructor does the same if anything throws.

**Tests (62 new):**
- *ThreadPool (19):* reports its worker count; zero workers rejected; exactly N distinct worker threads, all running simultaneously (a rendezvous that can only be satisfied by truly concurrent tasks; its timeout exists only so a broken pool fails instead of hanging); every task executed (2000); results and exceptions returned through futures (type and message preserved, pool survives); more tasks than workers never exceeds the worker count; shutdown and destructor run every queued task (checked while the single worker is provably busy and 50 tasks are provably queued); submit after shutdown; idempotent and concurrent shutdown; shutdown from a worker rejected; submission from 8 threads at once; 100 create/destroy rounds; futures valid after the pool is gone; FIFO start order.
- *Statistics (8):* empty, single value, known sample, identical values, two values, *t* table and approximation, precondition.
- *Seeds (5):* deterministic; 100,000 distinct seeds; depends on the index and is not base+index; different bases give disjoint sequences; adjacent seeds well mixed.
- *Study set-up (9):* zero and too many replications rejected, invalid simulation config rejected, worker-count resolution (1, 2, 8, 64 > 3 replications, 10⁶, automatic), `workersUsed`; one replication equals a direct Phase 5 run; **every one of 20 replications equals its standalone single-threaded run** (isolation); results ordered by index with 8 workers; ordering holds even when replication 0 deliberately finishes last.
- *Determinism (6):* **1, 2, 4 and 8 workers give identical replications and aggregates** (20 replications, base seed 12345); automatic workers; 64 and 100,000 workers for 3 replications; **10 repeated 4-worker studies and 3 repeated 8-worker studies identical**; `SimulationConfig::seed` ignored; different base seeds differ.
- *Independence and isolation (2):* 20 distinct seeds and 20 distinct arrival sequences; on a congested tiny-capacity network each of 24 replications starts with empty resources (its first request is accepted), and its final allocation and active count match an independent recomputation from its own trace.
- *Concurrency (3):* four replications rendezvous (so they are provably running at once) and then simulate on the *same* topology, matching sequential results; 32 replications over a shared 64-node topology on 8 workers, three rounds, topology unchanged; 12 raw threads sharing one topology.
- *Aggregation (4):* exact totals, ratios, means, weighted path averages and statistics on synthetic results (aggregate acceptance 23/40 = 0.575, not the mean of the rates); zero-request study gives zeros, no NaN; empty list; study totals equal the sum over replications.
- *Errors (5):* failing replications → lowest index reported, all 10 still executed, for 1 and 4 workers; exceptions (standard and non-standard) reported as `InternalError`; 20 failed studies leave nothing behind and the next study works; same error for every worker count.
- *Large (1):* 24 replications on an 8×8 grid (64 nodes, 112 links, about 600 requests each, ~14,000 in total): 1 worker and automatic workers give identical outcomes; aggregate checks hold.

**Repeated concurrent execution:** all 62 Phase 6 tests were run 40 times in a row in Release and 5 times in a row in Debug with no failure. No concurrency sanitizer exists for the MSVC toolchain used here, so none was run; ThreadSanitizer on GCC/Clang remains a worthwhile follow-up.

**Mutation checks performed (each caught, then restored and verified identical):** replication index ignored in the seed derivation (5 tests failed); results taken in completion order (8+ failed, including every 1-vs-N-workers comparison); allocation state shared between all `ResourceManager`s (many Phase 4/5/6 tests failed); aggregate acceptance computed as a mean of rates (2 failed); thread count not bounded by the replication count (2 failed); shutdown dropping queued tasks (a drain test failed and one timed out). Mutations that did not compile under warnings-as-errors were re-run in a separate build with that option off.

**Performance/correctness validation:** the 24-replication study above took about 0.30 s with one worker and 0.05 s with 12 workers (automatic) in Release on the development machine (12 hardware threads), with identical results; a larger 40-replication, 100-node-grid run (~60,000 requests) measured earlier gave 2.3 s versus 0.4 s. These are sanity measurements, not benchmarks (Phase 10). The workloads of the heavier study tests were sized so that the whole suite stays quick (Release ~9 s, Debug ~90 s).

**Test count and results:** 390 total = 43 Phase 1 + 78 Phase 2 + 74 Phase 3 + 59 Phase 4 + 74 Phase 5 + 62 Phase 6 (earlier tests unchanged). Clean rebuild and `ctest`: Debug 390/390 passed, Release 390/390 passed. Zero warnings under warnings-as-errors. C++20 confirmed in the compile commands. Toolchain unchanged (MSVC 19.44.35229, CMake 3.31.6, Ninja 1.13.0); only MSVC was tested.

**Important design decisions:** parallelism is across replications, never inside an event loop, so a run stays deterministic and lock-free; seeds are derived on the coordinator by an injective mixer; results are collected by index so scheduling cannot influence them; a single failing replication fails the study with a deterministic error; the topology stays immutable during a study (no locks were added to it); the injectable replication function keeps the coordinator testable without sleeps.

**Known limitations:**
- Replications are the only unit of parallelism: a single large simulation does not speed up with more threads.
- A failed study returns no partial results; nothing is cancelled early, so a long failing study still runs every replication.
- Results for all replications are held in memory (with `recordTrace` this can be large).
- `ThreadPool` is a plain FIFO pool: no priorities, work stealing, cancellation or resizing.
- No warm-up removal, batch means or sequential stopping rules; confidence intervals assume independent, roughly normal replication averages.
- A `linkFilter` supplied in the config must be thread-safe; this is documented, not enforced.
- Topology must not be mutated while a study runs; this is documented, not enforced.
- No ThreadSanitizer run (MSVC); only MSVC was tested.
- Failure injection and recovery are Phase 7 and do not exist yet.

**Commit:** `910020b085a8098bf544d1f5b276ad06f924b0ca` — `feat: implement OpticalNet phase 6 multithreaded simulation`.

**Next phase:** Phase 7 — Failure & Recovery Simulation (completed below)

## Phase 7 Summary

**Status:** Completed

> **Phase 7 models failures as a non-destructive overlay on the immutable topology. Failed resources become unavailable for routing and allocation without being deleted from the topology.**

**Objective:** Simulate link and node failures and their recovery inside the existing discrete-event engine: detect the active connections a failure hits, recover them under a configurable policy, and keep resources, connections and failure state consistent.

**Failure overlay architecture** (`include/opticalnet/failures/`, `src/failures/`, library `opticalnet::failures`, depends on `opticalnet::routing`):
```text
Immutable Topology  +  FailureState (failed links, failed nodes)  =  current network availability
```
`FailureState` is owned by one simulation run. A failed resource is *unavailable*, not *deleted*: its `LinkId`/`NodeId` stays valid (traces keep referring to it), the topology is never modified, and configured capacities are never touched. There is no global or shared failure state.

**Link failure / node failure:** `failLink`, `recoverLink`, `failNode`, `recoverNode`, `clearFailures`, `isLinkFailed`, `isNodeFailed`, `isLinkUsable`, `isNodeUsable`, `isPathAffected`, and ordered read-only `failedLinks()` / `failedNodes()`. A link is usable only if it is not failed **and neither end node is failed**, so a failed node makes every incident link unusable (routes cannot enter, leave or pass through it) without marking those links failed. Validation is explicit: unknown id → `NotFound`; failing a failed resource or recovering a healthy one → the new `InvalidTransition`; a failed call changes nothing.

**Routing integration:** no second routing algorithm. `ConnectionManager::setAvailability(predicate)` composes the failure check with the capacity check and the request's own filter through the Phase 3 `linkFilter` seam (`capacity ∧ availability ∧ caller filter`). When only the failure (not capacity) blocks a route the error stays `NoFeasibleRoute`, not `InsufficientCapacity`. `establishOnPath` rejects paths over unavailable links (new `ResourceUnavailable`). The simulation wires the predicate to the run's `FailureState`.

**Failure and recovery events:** `EventType` gained `LinkRecovery`, `NodeRecovery`, `LinkFailure`, `NodeFailure` in the same queue. Same-timestamp order (deterministic, documented, tested): **recoveries → failures → releases → arrivals** (links before nodes within each). State changes come first so that releases and arrivals at that instant see the new state; recoveries precede failures so a reroute at time *t* can use what recovered at *t* and a resource that recovers and fails again at one instant ends failed; the Phase 5 rule "release before arrival" is unchanged. Scripted failures are `ScriptedFailure{time, action, id}` in `SimulationConfig::failureSchedule`; the whole schedule is validated before the run by replaying it in event order (so a same-instant "fail then recover" of one link is rejected as recovering a healthy link).

**Failure detection:** after a failure is applied, the active connections (ascending id) whose current path uses a failed link or passes through/starts/ends at a failed node are the *affected* ones. A plain, deterministic scan of the registry; no extra index was needed.

**Recovery policy (`RecoveryPolicy`):** `NoRecovery` (affected connections are lost) or `Reroute` (default).

**Rerouting and atomicity:** all affected connections are first **detached** (`ConnectionManager::detach` releases exactly their stored allocation and hands back the `Connection`), which makes their freed capacity visible to every reroute; then, in ascending id order, each is **re-established** by the ordinary atomic `establish` with its *own* request: same `ConnectionId`, same demand, same source/destination and the original routing constraints (hop limit, cost limit, blocked nodes/links, caller filter are never relaxed). A reroute therefore either fully succeeds or leaves the connection lost with *nothing* allocated for it: no partial allocation, no leak, no double release. A rerouted connection stays active under its original id (its original scheduled end still applies). A connection that cannot be re-established (no route: `NoFeasibleRoute`; route but no free channels: `InsufficientCapacity`) is lost. `establishOnPath` remains available for precomputed backup paths and now also checks availability.

**Resource semantics during failure:** configured capacity is never changed (no "capacity zero"). A failed resource simply cannot be routed over or allocated on. Allocations on it stay attributed to their connection until that connection is detached by recovery handling (immediately, at the failure event), so there is no window in which a connection sits on a failed resource. At the end of every run the engine verifies that no active connection uses a failed resource, that the registry and per-link allocations agree, and that its own channel counts equal the `ResourceManager`'s; otherwise the run fails with `InconsistentState`.

**Connection lifecycle:** *active* (in the registry) or *lost* (not in the registry, tracked by the engine until its scheduled end). A lost connection stays lost when its resource recovers; when its scheduled end arrives the release is skipped (not counted as a release) and its id is free again; before that, a new request with the same id is rejected as `DuplicateId`.

**Metrics** (added to `SimulationMetrics`; all 0 when no failure is scheduled):
- `totalFailureEvents` (= `linkFailures` + `nodeFailures`), `linkRecoveries`, `nodeRecoveries`, `failedLinksAtEnd`, `failedNodesAtEnd`.
- `connectionsAffected`, `connectionsRerouted`, `connectionsLost` (affected = rerouted + lost), `recoverySuccessRate = rerouted / affected` (0 if none affected).
- `totalConnectionDowntime` = sum over *lost* connections of (min(scheduled end, end of run) − time lost). Rerouting completes inside the failure event and no signalling/restoration delay is modelled, so rerouted connections add exactly 0; therefore no "average recovery time" is reported (it would be a meaningless constant 0).
- Path statistics (`averagePathCost`, `averageHopCount`, `maxHopCount`) keep describing each connection's first path. The identity `accepted = activeAtEnd + releases + lost` holds.
- `StudyAggregate` adds `totalFailureEvents`, `totalConnectionsAffected/Rerouted/Lost`, `aggregateRecoverySuccessRate` (from totals) and `totalConnectionDowntime`.

**Trace** (optional, `recordTrace`): failure/recovery events appear as `TraceKind::Event` entries (type `LinkFailure`, …, with `resource` = the id); each affected connection then produces `Affected` followed (after all Affected entries) by `Rerouted` (with the new hop count) or `Lost` (with the reason, absent under `NoRecovery`). Example: `t=1 ARRIVAL #1 → t=5 LINK_FAIL link 2 → AFFECTED #1 → REROUTED #1 (3 hops)`.

**Deterministic failure fixtures (hand-verified):** with the 5-node topology A–B–C (2 hops) plus the detour A–D–E–C (3 hops), 10 channels per link:
- **A — link failure with alternate route:** #1 (A→C, 4 ch) on A–B–C; link B–C fails at t=5 → rerouted over the detour, same id and demand; final allocation 12 channel-links; utilization (8·4 + 12·5)/(50·10) = 0.184; trace Arrival → LinkFailure → Affected → Rerouted. (The same scenario is also checked at `ConnectionManager` level with exact node/link sequences on the 4-node square.)
- **B — no alternate route:** only route cut → connection lost, 0 channels left on the healthy link, downtime 5 (or 4 when its scheduled end, t=9, falls inside the run and is skipped).
- **C — link recovery:** link down → arrival rejected as `NoFeasibleRoute`; recovered → arrival accepted; with the detour topology the route is the detour while down and the short route after recovery.
- **D — node failure:** transit node B fails → #1 rerouted (3 hops), a request to B is rejected, a new A→C request avoids B (3 hops), after recovery A→C uses B again (2 hops); a failed destination loses the connection; the topology keeps node and links.
- **E — capacity-constrained reroute:** the detour exists but its first link is full → reroute fails with `InsufficientCapacity`, the connection is lost, the unrelated connection's 10 channels are intact, nothing partially allocated.

**Random failure model:** `RandomFailureConfig{failureRate, meanRepairTime, nodeFailureProbability}` (used only if the scripted schedule is empty): a Poisson process of failures over the whole network, each hitting a uniformly chosen currently-healthy node (probability `nodeFailureProbability`) or link, repaired after an exponential time (recoveries beyond the horizon are not generated). `generateRandomFailures` expands it into a concrete schedule from its **own RNG stream** (`splitmix64(seed + constant)`), independent of traffic and of the workload stream (a test shows enabling failures leaves the request sequence unchanged), always valid and only using existing ids. Scripted and random failures cannot be combined (`InvalidArgument`).

**Multithreaded integration:** each replication builds its own `FailureState`, `ConnectionManager`, queue and RNG streams (the random failure schedule is derived from the replication's seed); only the immutable topology and config are shared. `SimulationStudy` needed no structural change. Verified: 1, 2, 4, 8 and automatic workers give identical per-replication results with scripted and with random failures, repeated parallel runs are identical, each replication equals its standalone run, replications draw different failure schedules, and a scripted schedule applied in every replication (same link fails in all of them) never collides — which it would if overlays were shared.

**Tests (101 new):**
- *FailureState (15):* fail/recover link and node, unknown ids, repeated failure, recovery of healthy resources, fail-again-after-recovery, independent link/node id spaces, failed node disables incident links without marking them failed, `clearFailures`, ordered accessors, path-affected detection, topology untouched and capacity unchanged, independent overlays.
- *Schedule (8) / events (4) / random generator (7):* valid and invalid schedules (unknown ids, bad transitions, invalid times, entry named in the message, same-time recovery-before-failure semantics), random-config validation, queue ordering recoveries → failures → releases → arrivals, same-time sequence tie-break; generator determinism, validity (replays cleanly, only existing ids, within the horizon), node/link mix control, zero rate, single-resource.
- *Routing/recovery primitives at manager level (19):* failed link/node excluded, recovery restores routes, alternate route, no alternate route, capacity and failure combined, caller filter combined, `establishOnPath` over failed resources, `detach` semantics, fixtures A, B and E, hop-limit and blocked-node constraints preserved.
- *Engine (37):* fixtures A–E with exact metrics and traces, `NoRecovery`, unaffected connections, lost-connection skip/downtime/id reservation, demand and constraint preservation, ascending-id recovery order sharing freed capacity (`recoverySuccessRate` 0.5), repeated reroutes, event ordering with failure+arrival, recovery+arrival, failure+release, recovery+failure, recover-then-fail at one instant, failures beyond the horizon, invalid schedules (scripted and generated runs), scripted+random conflict, random failures (same seed identical, workload unchanged, consistency, policies).
- *Study (10 + 1 large):* described above, plus a **large study**: 12 replications on an 8×8 grid (64 nodes, 112 links), ~800 requests each (> 6000 in total), random link and node failures (> 120 failure events, > 50 affected connections), 1 worker vs automatic workers identical, and every replication's final allocation and active count recomputed independently from its trace.

**Mutation checks performed (each caught, then restored and verified identical; run with warnings-as-errors off so the mutants compile):** failed link still routable (many failures); failed node still routable; affected connections not detected; `ConnectionId` changed on reroute; recovery ignoring capacity (Phase 4–7 tests); recovery ignoring routing constraints (3 tests); lost connection leaving its allocation behind (caught by the end-of-run consistency check and tests); recovery event not restoring availability; failure/recovery event order changed (4 tests); failure state shared between replications (a `static` overlay; study tests failed).

**Test count and results:** 491 total = 43 Phase 1 + 78 Phase 2 + 74 Phase 3 + 59 Phase 4 + 74 Phase 5 + 62 Phase 6 + 101 Phase 7 (earlier tests unchanged). Debug and Release results are recorded below under "Final build results".

**Important design decisions:** failures are an overlay owned by the run, not topology edits; routing learns about them only through the existing filter seam; recovery = detach-all then re-establish through the existing atomic `establish`, which gets same-id/same-demand/same-constraints/atomicity for free and adds no second allocator; recoveries are ordered before failures at equal times; failed endpoint nodes simply make requests infeasible; random failures use a separate RNG stream; the end-of-run cross-check turns any inconsistency into an error instead of a silently wrong result.

**Known limitations:**
- Recovery is instantaneous and "all or nothing" per connection: no restoration delay, no partial/degraded service, no protection (pre-reserved backup) paths, no shared-risk groups, no correlated failures.
- Lost connections are never re-established when their resource recovers (the policy acts only at failure time).
- Reroute order is by ascending connection id, which is deterministic but not "fair" or priority-aware.
- A reroute is attempted with the Dijkstra router and the original metric; it does not optimise for minimal disruption or for load balance.
- `totalConnectionDowntime` counts only lost connections; there is no per-connection availability figure.
- The random model fails at most one resource at a time per event and does not model failure-rate differences between resources.
- Failure detection scans the active connections (O(active) per failure), which is fine at the tested scale but not indexed.
- A user `linkFilter` in the config is still called from several threads in a study and must be thread-safe.
- Only MSVC was tested; no ThreadSanitizer run.

**Commit:** `217be79114d825a35937adf529f19fa754292eb5` — `feat: implement OpticalNet phase 7 failure and recovery simulation`.

**Next phase:** Phase 8 — Validation & Testing Framework (completed below)

**Final build results:** clean rebuild from scratch and `ctest`: **Debug 491/491 passed** (~121 s), **Release 491/491 passed** (~11 s). Zero warnings under warnings-as-errors; C++20 confirmed in the compile commands. Toolchain unchanged (MSVC 19.44.35229, CMake 3.31.6, Ninja 1.13.0). All 101 Phase 7 tests were additionally run 30 times in a row in Release and 3 times in a row in Debug with no failure. The large failure study (12 replications run sequentially and in parallel, with trace-based verification) takes about 0.4 s in Release.

## Phase 8 Summary

**Status:** Completed

> Phase 8 provides **project-specific validation** of OpticalNet's behaviour: independent invariant checkers, reference models, deterministic randomized testing and end-to-end scenarios. It is not a generic testing framework and it does not replace GoogleTest, which remains the test runner.

**Objective:** Turn the existing unit tests into a systematic answer to "how do we know OpticalNet is correct across topology, routing, resources, simulation, failure/recovery, determinism and large workloads?"

**Validation architecture** (`include/opticalnet/validation/`, `src/validation/`, library `opticalnet::validation`; tests in `tests/validation/`, built as their own executable `opticalnet_validation_tests`, discovered by CTest, with hand-computed fixtures in `tests/validation/fixtures/*.json`):
```text
Production components (core, topology, routing, resources, simulation, failures, concurrency)
        │   checked, never modified, by
        ▼
Validation layer:  Invariants │ Reference models │ Generators │ Scenario / randomized / stress / negative tests
```
The validation executable is separate so the validation layer can be run — and mutation-tested — without the unit tests. The validation library is not linked into any production target.

**Invariant system** (`Invariants.hpp`; each checker returns a `ValidationResult` listing named failures with ids and expected/actual values, and recomputes what it checks from raw data instead of asking the production component):
- `checkTopology` — referenced nodes/links/equipment exist, ids unique and equal to their map keys, no self-links, positive capacity/length/cost, the adjacency index (outgoing, incident, degree, LinkId ordering, directed vs. bidirectional rules) recomputed independently from the link list, unique equipment attachment, port constraints (optional).
- `checkFailureState` — failed ids exist; a link is unusable exactly when it, or an end node, is failed.
- `checkPath` — correct source/destination, a simple chain of existing links each traversable in the direction used, `hopCount` == number of links, cost recomputed link by link under the selected metric (with the metric formulas written out again, not `linkCost()`), parallel-link identity, and (when given) hop/cost limits, blocked nodes/links, minimum capacity, link filter, no failed link or node.
- `checkResources` — per link `0 ≤ allocated ≤ capacity` and `allocated + available == capacity`; `totalAllocated` == sum of per-link allocations, `totalCapacity` == sum of capacities, `totalAvailable == totalCapacity − totalAllocated`; no allocation on unknown links.
- `checkConnections` — every active connection has positive demand and a valid feasible path from its source to its destination avoiding failed resources; an independent shadow ledger summing the demands of all active connections equals the `ResourceManager`'s allocation on every link (so lost connections provably hold nothing).
- `checkSimulationTrace` — replays the optional trace with an independent ledger and checks time never goes backwards, same-timestamp events follow recoveries → failures → releases → arrivals, every release/reroute/loss refers to an active/affected connection, capacity is never exceeded, and recomputes accepted/rejected/rejection reasons/releases/failure and recovery counts/affected/rerouted/lost/peak and final allocation/active connections/hop statistics/rates/recovery rate and the **time-weighted utilization** — then compares all of them with the reported metrics.

**Independent reference models** (`ReferenceModels.hpp`): `exhaustiveRoute` enumerates every simple path (over individual links) with its own direction rule, cost formula and constraint checks (hop/cost limits, blocked elements, capacity, link filter, failure overlay), scanning the raw link list rather than the adjacency index; `reachableByAnyPath` (to tell `NoRoute` from `NoFeasibleRoute`); `ShadowLedger`, a separate all-or-nothing allocation model. At scale (5000 nodes) the production Dijkstra is compared with a queue-based **Bellman-Ford** over the raw link list — a different algorithm — instead of itself.

**Deterministic generators** (`Generators.hpp`, built on the existing `Rng`, explicit seeds, always valid data): `generateRandomTopology` (weakly connected; directed, parallel and bidirectional links; integer weights so ties are real; switches with enough ports; transceivers), `generateRandomConstraints`, `generateRandomRequests`, `generateRandomFailureScript` (tracks state itself, independent of the production random-failure model). Randomized tests print the seed (`SCOPED_TRACE`), topology size, source/destination, metric and limits so any failure can be reproduced; they have fixed iteration counts and finish quickly.

**Randomized validation (tests):**
- *Topology:* 100 random topologies + 15 larger ones satisfy every invariant; 25 random mutation sequences × 60 operations (add/remove nodes and links, deliberately invalid operations) keep the invariants and rejected operations leave the fingerprint unchanged; JSON round trips; failure overlays leave the topology byte-identical.
- *Routing:* **production Dijkstra == exhaustive enumeration** on 100 random topologies × 20 random constrained requests each (half with a failure overlay), comparing optimal cost, fewest hops and the error class (`NoRoute` iff unreachable), validating every returned path; cost and hop limits are exact at the boundary; results independent of link insertion order.
- *Resources:* 50 seeds × 300 random allocate/release operations (including invalid ones, repeated links and over-release) agree with the shadow ledger on every outcome and error code, per link, after every operation.
- *Connection layer (cross-layer):* 40 seeds × 120 random establish/release/fail/recover operations with the failure overlay; every establish is compared with an oracle that knows the free capacity, the constraints and the failures (success/cost/hops and the error class `InsufficientCapacity`/`NoFeasibleRoute`/`NoRoute`), the returned path is the registered and allocated one, and `checkConnections`/`checkResources`/`checkFailureState` run after every operation.
- *Simulation:* 40 generated runs, 40 generated runs with random failures, and 40 scripted runs with random failure scripts are independently replayed from their traces; a `ConnectionManager` driven only from a trace accepts exactly the connections the simulation accepted.
- *Failure/recovery:* a 40 × 120-step random walk of the failure state against an independent model (including invalid transitions and unknown ids), 40 random failure scripts through full simulations, no leaks between replications.

**End-to-end scenarios (tests):** (1) normal lifecycle, (2) congestion → alternate route then rejection, never over-allocation, (3) link failure → detection → reroute under the same `ConnectionId`, (4) node failure → alternate route → recovery restores the route, (5) unrecoverable connection is lost with nothing leaked, (6) a parallel study equals its single-worker execution; plus congestion combined with failure, constraints surviving recovery, and same-timestamp event ordering.

**Determinism validation:** same topology + config + seed → identical trace, metrics, failure sequence; studies with 1, 2, 4, automatic and 64 workers give identical per-replication results and aggregates (with and without random failures); repeated parallel studies identical; each replication equals its standalone run; replication seeds are distinct and independent of the worker count; failures do not change the request workload. Different seeds are *not* required to differ.

**Golden / reference scenarios:** nine hand-computed fixtures (`simple_line`, `triangle`, `ring`, `parallel_links`, `directed_graph`, `capacity_congestion`, `link_failure`, `node_failure`, `reroute`) with exact expected paths, links, costs and outcomes in `GoldenFixtureTests.cpp` (e.g. the triangle picks a different route under each metric; the ring tie-breaks to the smaller predecessor; parallel links keep their ids).

**Negative testing:** invalid nodes, links, connections, constraints, paths, duplicate ids, impossible capacities, unknown resources, invalid failure transitions, invalid simulation/failure/study configurations and invalid scripts are all verified to return the documented `Result` error codes, without throwing (programming errors such as a null algorithm, popping an empty queue or invalid generator configuration do throw, and are tested as such), and to leave the state unchanged.

**Stress / stability (bounded, correctness only):** routing on a 5000-node graph versus Bellman-Ford; 20,000 allocation/release operations against the ledger; ~5000 simulated requests replayed from the trace; ~800 failure/recovery events with hundreds of affected connections; 24 replications on 8 workers with failures equal to the 1-worker run and each validated; 2500 scripted requests with 150 scripted failure operations.

**Validator self-tests (24):** the validators are shown to notice problems — fabricated paths with wrong cost, endpoints, missing/disconnected/backward links, failed or blocked elements, constraint violations, repeated nodes and metric mismatches; over-capacity and orphan allocations; a connection left on a failed resource; a path invalidated by a topology change; tampered metrics (16 different fields) and tampered traces; the reference models on hand-checked cases.

**Mutation testing (validation suite only; production code mutated with warnings-as-errors off, then restored and verified byte-identical).** All 14 were caught:
1 ignore failed links (29 validation tests failed) · 2 ignore failed nodes (21) · 3 allow allocation above capacity (4) · 4 incorrect total allocation (38) · 5 incorrect path cost (20) · 6 incorrect hop count (45) · 7 ignore maxHops (5) · 8 ignore maxCost (4) · 9 incorrect connection release (27) · 10 incorrect simulation utilization (20) · 11 incorrect event ordering (**initially not caught** — the suite had no same-timestamp ordering check; fixed by adding the order check to `checkSimulationTrace` and an ordering scenario, after which 4 tests fail) · 12 failure state shared between replications (42) · 13 incorrect seed derivation (1) · 14 reroute relaxing the connection's constraints (1; an engine-level scenario was added because the connection-level random test mirrors the engine's logic rather than running it).

**Test count and results:** 596 total = 43 Phase 1 + 78 Phase 2 + 74 Phase 3 + 59 Phase 4 + 74 Phase 5 + 62 Phase 6 + 101 Phase 7 + 105 Phase 8 (all earlier tests unchanged). The 105 Phase 8 tests: validator self-tests 24, topology 8, routing 6, resources 3, connection layer 4, simulation 6, failures 6, determinism 7, scenarios 10, golden fixtures 9, negative inputs 9, stress 6, generators 7. Clean rebuild and `ctest`: **Debug 596/596 passed** (~103 s with `ctest -j 4`; the validation executable alone ~85 s in Debug), **Release 596/596 passed** (~16 s). The validation suite also passed 15 consecutive runs in Release. Zero warnings under warnings-as-errors; C++20 confirmed in the compile commands. Toolchain unchanged (MSVC 19.44.35229, CMake 3.31.6, Ninja 1.13.0); only MSVC was tested.

**Important design decisions:** validators recompute from raw data and use different algorithms from the code they check (exhaustive enumeration, Bellman-Ford, shadow ledgers, trace replay); the validation executable is separate so it can be mutation-tested alone; randomized tests use fixed seeds, bounded iteration counts and coverage assertions (each outcome class must actually occur); every validator has a self-test showing that it detects violations; no new dependency, no global state.

**Known limitations:**
- Exhaustive routing comparison is limited to small graphs (≤ ~7 nodes); larger graphs use the Bellman-Ford cross-check for cost only.
- The trace carries hop counts, not paths, so the trace replay cannot verify individual paths or total connection downtime; those are validated at the manager level and by the unit tests.
- Randomized tests cover seeds 1..N only; they are bounded, not exhaustive, and are not a substitute for formal verification.
- No coverage measurement, sanitizers or fuzzing tool (no MSVC ThreadSanitizer); only MSVC was tested.
- The Debug run of the validation executable is slow (~85 s) because of the stress tests.

**Commit:** `580969c59c7a102083393bd5585a5dc458ccce16` — `test: implement OpticalNet phase 8 validation framework`.

**Next phase:** Phase 9 — Python + FastAPI Integration (completed below)

## Phase 9 Summary

**Status:** Completed

> Phase 9 makes the C++ engine usable from Python and over HTTP. **Python is an interface layer only**: no algorithm (graph, routing, resource accounting, simulation, failures, studies, validation) was re-implemented in Python, and no C++ production source was modified. Every number returned by the API was produced by the C++ engine.

**Architecture**
```text
HTTP client
   │
FastAPI  (api/main.py, api/routes/*)          thin routers, OpenAPI at /docs and /openapi.json
   │
Pydantic (api/models.py)                      request/response shape and range checks only
   │
Python package `opticalnet` (python/opticalnet)   re-exports the extension, stable exception classes
   │
pybind11 (src/python/bindings.cpp)            module `opticalnet._opticalnet`
   │
C++ OpticalNet: Topology · Routing · Resources · Simulation · Failures/Recovery · Study (thread pool) · Validation
```
- **Binding module** (`src/python/bindings.cpp`, CMake option `OPTICALNET_BUILD_PYTHON`, default OFF, so the C++ libraries and tests never need Python; pybind11 2.13.6 via `FetchContent`). Output: `python/opticalnet/_opticalnet.<tag>.pyd|so` (git-ignored), or any directory given by `OPTICALNET_PYTHON_OUTPUT_DIR`.
- **IDs** are plain Python ints (range-checked to `uint32`; out-of-range values raise `InvalidArgumentError` instead of wrapping). Enums are strings (`hop_count|distance|administrative`, `exponential|fixed`, `reroute|no_recovery`, `fail_link|recover_link|fail_node|recover_node`). Results are dicts/lists of plain values with snake_case keys; `Result<T>`, `StrongId<Tag>` and raw pointers are never exposed.

**Exposed C++ functionality**
- *Topology:* `Topology` (`from_json`, `from_file`, `to_json`, `nodes()`, `links()`, `transceivers()`, `switching_elements()`, `node(id)`, `link(id)`, `neighbors`, `links_between`, `is_reachable`, `is_connected`, `is_strongly_connected`, `connected_components`, `validate`), `load_topology`, `topology_from_json`. Parsing is done by the C++ loader, so the strict Phase 2 schema (unknown keys rejected) stays the single schema.
- *Routing:* `route(...)` / `RoutingConstraints` + `route_with(...)`: metric, `max_cost`, `max_hops`, `min_link_capacity`, blocked nodes/links. The `linkFilter` callable is deliberately not exposed (a Python callback invoked from C++ threads would need the GIL and break determinism); capacity-aware routing is available through `ConnectionSession`.
- *Resources/connections:* `ConnectionSession` wraps one C++ `ConnectionManager` (+ its `ResourceManager` and a `FailureState` overlay used as the availability predicate): `establish`, `release`, `connection(s)`, `usage(s)`, `totals`, `fail_/recover_link|node`, `validate` (manager validation plus the Phase 8 `checkResources`/`checkConnections`). No Python-side ledger.
- *Simulation:* `SimulationConfig` (seed, end time, max requests, arrival rate/timing, mean lifetime/timing, demand range, routing, failure schedule, random failures, recovery policy, trace), `ScriptedArrival`, `simulate(topology, config, arrivals=None)` → seed + full `SimulationMetrics` (+ optional trace).
- *Failures/recovery:* scripted (`ScriptedFailure`) and random (`RandomFailureConfig`) failures, `reroute`/`no_recovery`; affected/rerouted/lost counts, recovery success rate, downtime come straight from C++.
- *Studies:* `run_study(topology, config, replications, base_seed, workers)` calls `SimulationStudy::run`; the C++ pool owns all threads. Returns per-replication results, the full `StudyAggregate` (totals, means, std dev, standard error, 95% CI [`mean ± half-width`, formed from the C++ half-width]) and `workers_used`.
- *Validation:* `validate_topology` (`Topology::validate` + `checkTopology`) and `validate_simulation` (traced run + `checkSimulationTrace`).

**Error mapping.** Each C++ `ErrorCode` has a Python exception class (subclass of `OpticalNetError`) with a stable `code` string, so `NotFoundError`, `NoRouteError`, `NoFeasibleRouteError`, `InsufficientCapacityError`, `InvalidTransitionError`, `ResourceUnavailableError`, … stay distinguishable. HTTP mapping (`api/errors.py`): `NOT_FOUND`, `NO_ROUTE`, `NO_FEASIBLE_ROUTE` → 404; `INVALID_ARGUMENT`, `PARSE_ERROR` → 422; `DUPLICATE_ID`, `CONSTRAINT_VIOLATION`, `INSUFFICIENT_CAPACITY`, `INVALID_TRANSITION`, `RESOURCE_UNAVAILABLE` → 409; `INCONSISTENT_STATE`, `INTERNAL_ERROR` → 500. Pydantic failures → 422 `VALIDATION_ERROR` (with field locations); oversized bodies → 413; a topology document that the C++ loader rejects is always 422 (it is the request body that is wrong) with the engine's code kept. Response shape: `{"error": {"code": ..., "message": ...}}`. Unexpected exceptions return a generic `INTERNAL_ERROR` (details stay in the server log) and are never swallowed.

**Ownership / lifetime.** `Topology` is immutable and held by `std::shared_ptr<const Topology>`. `ConnectionSession` co-owns that pointer (declared before the `FailureState` and `ConnectionManager` that reference it) and is registered with `keep_alive`, so a topology cannot disappear while a manager depends on it (tested: `del topology; gc.collect()` then keep using the session). Every returned value is a copy into Python objects; nothing aliases C++ internals.

**Thread-safety / GIL.** `simulate`, `run_study`, `route`, `validate_*` and topology parsing copy their inputs, release the GIL (`py::gil_scoped_release`) and touch only C++ data, so FastAPI's worker threads run simulations truly concurrently (tested: 8 threads give results identical to serial runs; a ticking Python thread keeps running during a long simulation). The only shared object in the API is the in-memory `TopologyStore` (immutable topologies, one lock, at most 64 entries). Connection managers, failure state and simulations are created per request and destroyed with it — the `/connections/scenario` endpoint applies a list of operations in one request instead of keeping a session between requests. `ConnectionSession` itself is not thread-safe (documented); the module holds no mutable global state.

**Endpoints.** `GET /health`; `POST /topologies` (201, body = topology document), `GET /topologies`, `GET|DELETE /topologies/{id}`, `GET /topologies/{id}/export|nodes|links`, `.../nodes/{id}`, `.../links/{id}`; `POST /route`; `POST /connections/scenario`; `POST /simulate`; `POST /study`; `POST /validate`. Every operation accepts either `topology_id` or an inline `topology` document (parsed by C++). `/docs` and `/openapi.json` are served with summaries, descriptions and typed request/response/error models. Run: `uvicorn api.main:app --reload`.

**Pydantic models** (`api/models.py`): range checks on ids (uint32), demand ≥ 1, times, rates, `min_demand ≤ max_demand`, enum literals, worker count 0..64, replications 1..10,000, scripted/random failures mutually exclusive, bounded work per request (`end_time × arrival_rate` ≤ 1,000,000), `extra="forbid"` everywhere. Domain correctness (does a node exist, is a failure transition valid, …) is left to C++.

**Security/safety.** No shell, code or dynamic imports from input; no filesystem paths accepted (topologies arrive as JSON, never as paths); body size limited (8 MB, topology 4 MB); no addresses or C++ exception strings in responses; no authentication (local service, out of scope).

**Build and run**
```bash
pip install -r requirements.txt
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DOPTICALNET_BUILD_PYTHON=ON
cmake --build build                       # builds the C++ libraries, tests and the extension
python -m pytest tests/api -q             # or: ctest --test-dir build  (includes python_api_tests)
uvicorn api.main:app --reload
```
Dependencies: Python ≥ 3.10 (tested 3.10.11), FastAPI (tested 0.115.6), Pydantic (2.13.4), Uvicorn (0.34.0), pybind11 2.13.6 (CMake `FetchContent`), pytest 8.3.4 and httpx 0.28.1 for tests.

**Tests** (`tests/api`, pytest + FastAPI `TestClient`; no mocks of the engine except where an engine exception is injected to test error mapping): **102 Python tests** — binding 22 (compiled-extension check, topology introspection, routing mapping, every error class, session lifecycle and failure overlay, keep-alive, config properties, determinism, study = standalone runs, concurrency/GIL, validators), topology+routing API 29 (health, docs/OpenAPI, valid/invalid topology documents incl. unknown keys, nodes/links, store limit, directed and parallel links, constraints, error codes), connections 9, simulation/failure/study/validation 24, error mapping and end-to-end 18 (all 12 codes → status, unexpected errors don't leak, the HTTP → C++ → JSON chain on `configs/ring4.json`, concurrent requests). Failure tests cover link/node failure with reroute, no-recovery loss, second failure losing a rerouted connection (downtime 44), recovery, invalid schedules and random failures; study tests cover determinism across 1/2/4/auto workers, `workers_used`, replication = standalone run for its derived seed, and failure aggregates.

**C++ regression:** no C++ production or test source changed: the 596 GoogleTest cases (43+78+74+59+74+62+101+105) are untouched; new C++ tests: 0. CTest with the extension enabled has 597 tests (596 + `python_api_tests`, which runs the 102 pytest cases).

**Integration mutation testing** (binding/API deliberately broken, Python suite must fail, then restored byte-identical and rebuilt; 17/17 caught, failing tests in parentheses): swapped source/destination in C++ (9) and in the API (8); wrong metric mapping (7); dropped blocked-node constraint (4); wrong error-code mapping (6); wrong `hop_count` serialization (4); wrong worker count (4); base seed not propagated in a study (1); demand altered on establish (7); confidence-interval sign (1); NO_ROUTE HTTP status (6); simulation seed not propagated (4); dropped failure schedule (7), recovery policy (3), random failures (3), `min_link_capacity` (2), blocked links (3). Three of the C++ mutations first failed to compile under warnings-as-errors (unused parameter) and were redone in a warning-free form.

**Build/validation results:** clean configure+build of Release and Debug with warnings-as-errors, zero warnings, C++20. **Release: 596/596 GoogleTest cases passed** (~12 s with `-j4`) and `python_api_tests` passed (102/102 pytest cases, ~12 s under CTest). **Debug: 596/596 passed** (~121 s) and `python_api_tests` passed against the Debug extension (~177 s). CTest total with the extension enabled: 597 entries. Python/API: **102/102** (the suite was run repeatedly without a failure). A real `uvicorn api.main:app` server answered `/health`, `/docs`, `/openapi.json` (all 200) and a real `POST /route` against `configs/ring4.json` (path `[1,2,3]`, cost 300, 2 hops). A default configure (`OPTICALNET_BUILD_PYTHON=OFF`) builds the C++ libraries and tests without Python. Toolchain unchanged (MSVC 19.44.35229, CMake 3.31.6, Ninja 1.13.0).

**Important decisions:** pybind11 (as planned) over ctypes/CFFI/subprocess; dicts and strings instead of exposing C++ classes (stable, JSON-friendly, nothing to dangle); one request-scoped C++ state instead of server-side sessions; threads only in C++; the extension is opt-in via CMake; errors keep the engine's stable code.

**Known limitations:**
- Only Windows/MSVC/Python 3.10 was tested; the extension must be built for the Python it will run under; no wheel/packaging (release packaging is Phase 10).
- `RoutingConstraints::linkFilter` is not exposed; topologies are immutable once loaded (no mutation API; submit a new document).
- The Debug extension links the release Python library (standard pybind11 behaviour); the Python tests pass against both the Release and the Debug extension (Debug is slow: ~177 s).
- Results carry no persistence and the topology store is in memory (max 64); no authentication; response times are not benchmarked.
- `include_trace` on large runs produces large responses (the trace is only returned on request, and never for studies).
- `CONSTRAINT_VIOLATION` and `RESOURCE_UNAVAILABLE` have exception classes and HTTP mappings but are not reachable through the current API surface (they are tested via injected exceptions).

**Commit:** `44755a77ac020d018bfe3273415c592b03da4d19` — `feat: implement OpticalNet Python and FastAPI integration`.

**Next phase:** Phase 10 — Optimization, Benchmarking & Release (completed below)

## Phase 10 Summary

**Status:** Completed

> Phase 10 measured the engine, optimized only what the measurements pointed at, re-validated everything, and prepared the project for demonstration. No feature was added. Every number below was measured by the benchmark suite in this repository on the environment stated; none is estimated.

**What was implemented**
- **Benchmark suite** (`benchmarks/`, CMake option `OPTICALNET_BUILD_BENCHMARKS`, default OFF): `Bench.cpp` (C++, executable `opticalnet_bench`), `bench_api.py` (binding vs. in-process FastAPI `TestClient`), `bench_server.py` (a real `uvicorn` server over loopback HTTP, standard-library client), `report.py` (turns the raw JSON of before/after runs into `benchmarks/RESULTS.md`, including a bit-identity check of the computed results).
- **Three measured optimizations** (below) plus **10 new C++ regression tests** (`tests/unit/OptimizationTests.cpp`).
- **Demo** (`examples/demo.py`): a narrated end-to-end walkthrough of the HTTP API (topology, routing with metrics/constraints, connection capacity, simulation + determinism, failure/recovery under both policies, study with 1 vs 4 workers, validation), runnable against a server or `--in-process`.
- **Final documentation** (README rewritten as the project's front page with a recruiter demo, this report, Phases/ARCHITECTURE/TECHSTACK updated).
- **Linux/GCC portability fixes** found by the first build on GCC (details under *Codespace findings*).

**Benchmark methodology.** Workloads are generated, deterministic grid topologies and fixed seeds (no I/O in the timed regions; request endpoints, blocked nodes and simulation seeds come from seeded generators). Each benchmark runs 3 repetitions and the **median** is recorded; "before" (commit `44755a7`, built in a separate git worktree with the same compiler flags and the same benchmark source) and "after" were run **alternately** (before, after, before, after) on the same machine, and the reported value is the mean of the two per-run medians. Speedup = before / after. Each benchmark also folds its computed results (path nodes/links/costs, all simulation metrics, trace entries, study aggregates) into a checksum, which is compared across all runs. Memory is the process peak resident size, measured in a separate process per workload (`wait4` rusage), because a single process only reports a cumulative peak.

**Environment.** GitHub Codespace, 4 vCPU AMD EPYC 7763, 16 GB RAM, Ubuntu 24.04; GCC 13.3 (`-O3`, LTO, `-std=c++20`, `-Wall -Wextra -Wpedantic -Werror`), CMake 3.28.3, Ninja 1.11.1, Python 3.13.8, FastAPI 0.162.0, Pydantic 2.14.1, Uvicorn 0.54.0, pytest 9.1.1, httpx 0.28.1, pybind11 2.13.6. (The first, abandoned attempt on the Windows development laptop — MSVC 19.44, 12 hardware threads, 7.3 GB RAM — was stopped by the OS under memory pressure; its quick preliminary figures of roughly 2.5x routing and 1.9x simulation were *not* used.)

**Results (C++ engine; complete tables in `benchmarks/RESULTS.md`)**

| Benchmark | Workload | Before | After | Speedup | Results identical |
|---|---|---:|---:|---:|---|
| routing.dijkstra.hop_count | 100x100 grid (10,000 nodes, 19,800 links), 1,000 random pairs | 2,523 ms | 942 ms | 2.68x | yes |
| routing.dijkstra.distance | same | 2,812 ms | 1,152 ms | 2.44x | yes |
| routing.dijkstra.administrative | same | 2,823 ms | 1,150 ms | 2.46x | yes |
| routing.constrained.max_hops | maxHops=200 | 2,831 ms | 1,151 ms | 2.46x | yes |
| routing.constrained.tight_hops | maxHops=150, layered search | 7,927 ms | 3,457 ms | 2.29x | yes |
| routing.constrained.blocked_nodes | 485 blocked nodes + min capacity 8 | 2,968 ms | 1,361 ms | 2.18x | yes |
| connections.establish_release | 30x30 grid, 3,000 capacity-aware establishes + releases | 1,196 ms | 466 ms | 2.57x | yes |
| simulation.single | 20x20 grid (400 nodes, 760 links), 40,195 requests, 80,220 events | 4,536 ms | 2,004 ms | 2.26x | yes |
| simulation.single_traced | same, trace recorded | 4,558 ms | 1,992 ms | 2.29x | yes |
| simulation.failures | random failures: 608 failure events, 2,087 connections affected | 4,979 ms | 2,254 ms | 2.21x | yes |
| study, 1 / 2 / 4 / 8 / auto workers | 32 replications, 764,405 events | 43.89 / 22.12 / 16.17 / 16.47 / 16.25 s | 19.17 / 9.67 / 6.84 / 6.82 / 6.81 s | 2.29 / 2.29 / 2.36 / 2.42 / 2.39x | yes |
| resources.allocate_release | 30x30 grid, 200 paths x 500 rounds | 446 ms | 458 ms | 0.97x | n/a |
| topology.construct / export_json / load_json | 100x100 grid / 3.8 MB JSON | 9.5 / 77.6 / 79.8 ms | 10.4 / 76.0 / 81.0 ms | 0.91 / 1.02 / 0.99x | n/a |
| topology.connectivity / validate / neighbors | 100x100 grid | 7.0 / 1.74 / 0.73 ms | 7.4 / 1.93 / 0.90 ms | 0.95 / 0.90 / 0.81x | n/a |
| validation.check_topology / check_trace | 10,000 nodes / 25,337-entry trace | 20.6 / 10.2 ms | 20.6 / 10.3 ms | 1.00 / 1.00x | n/a |

After-optimization throughput: 868 routing queries/s on the 10,000-node grid, about 40,000 simulation events/s. **Study scaling (after):** 1 worker 19.17 s, 2 workers 1.98x, 4 workers 2.80x, 8 and "auto" (4 threads on 4 vCPUs) 2.81x/2.82x — flat beyond the 4 available vCPUs, and identical results for every worker count.

**Python / FastAPI (after vs. before, in-process `TestClient`, real compiled extension):** `binding.route` 61.8 → 26.6 ms for 300 routes (2.33x); `binding.simulate` 1,235 → 603 ms (2.05x); `POST /simulate` 1,225 → 561 ms (2.18x); with failures 1,359 → 668 ms (2.04x); `POST /study` 16 replications 1 worker 11.47 → 5.45 s (2.11x), 4 workers 4.19 → 1.92 s (2.18x); `POST /validate` 252 → 121 ms (2.08x); `POST /connections/scenario` (300 establishes) 114 → 63.9 ms (1.78x); `POST /topologies` (900 nodes, 181 KiB) 20.3 → 21.2 ms (0.96x); **`POST /route` 1,214 → 1,213 ms (1.00x)**: the in-process `TestClient` itself costs 3.54 ms for an empty `GET /health`, and a routed request costs 3.99 ms, while the engine needs only 0.087 ms of it — so the routing gain is invisible through HTTP in that harness (an honest finding, not a defect). 16 simulation requests: sent sequentially 7.97 s before / 3.61 s after; sent from 8 client threads 2.92 s before / 1.30 s after (2.25x faster than before; 2.8x faster than sequential after) — the GIL is released while C++ runs.

**Real server (uvicorn on loopback, after):** `GET /health` 0.86 ms; `POST /route` (900 nodes) 1.30 ms; `POST /simulate` (≈20,000 events) 552 ms; `POST /study` (16 replications) 5.31 s with 1 worker and 1.87 s with 4 workers (2.84x); 16 `/simulate` requests: 3.54 s sequentially vs 1.27 s from 8 concurrent clients (2.79x).

**Memory (peak resident, separate process per workload):** setup (100x100 grid) 16.2 MiB before and after; routing and study workloads 16.2 MiB both; simulation 17.8 MiB before, 18.6 MiB after (+0.8 MiB from the cached link pointers).

**Optimizations performed (each found by the measurements: routing is where the time goes — a simulation is mostly one Dijkstra per arrival)**
1. **Cached link pointer in `Adjacency`** (`include/opticalnet/topology/Topology.hpp`, `src/topology/Topology.cpp`, `src/routing/DijkstraRouter.cpp`). Dijkstra called `Topology::findLink` (a `std::map` lookup, ~log₂ 20,000 cache-missing steps) for every edge examined; each adjacency entry now carries `const FiberLink* fiber` into the owning `Network`'s map (nodes of a `std::map` never move). Safety: `Adjacency::operator==` ignores the pointer (so existing tests and semantics are unchanged); `Topology`'s copy constructor/assignment now **rebuild the index from a copy of the `Network`** so a copy can never point into the original (moves keep the map nodes, so pointers stay valid); `addLink` indexes the *stored* object, not the caller's temporary; the search falls back to `findLink` if a hand-built entry has no pointer. This accounts for the largest part of the routing gain (applied after the label table; on the quick workload it took distance routing from 70 → 32 ms).
2. **Flat Dijkstra label table** (`src/routing/DijkstraRouter.cpp`): the node-based `std::unordered_map<State, Label>` (one allocation per label, repeated rehash while a search grows to thousands of labels) is replaced by a contiguous `std::vector<Label>` indexed through an open-addressing table (linear probing, power-of-two capacity, load ≤ ½). Tie-breaking logic (`improves`) is untouched. Empty `blockedNodes`/`blockedLinks` sets are no longer hashed per edge. Measured on the quick workload: 78 → 70 ms before optimization 1.
3. **Hash map for per-link allocation** (`ResourceManager::allocated_`: `std::map` → `std::unordered_map`): `allocated(link)` is called for every edge by the capacity filter inside capacity-aware routing. Everything that exposes an order sorts by `LinkId` explicitly (`allocations()`, `validate()`), so output order is unchanged. Together with (1) this is what takes `connections.establish_release` to 2.57x and the simulation to 2.26x (simulation measured 376 → 270 → 192 ms on the quick workload after optimizations 1 and 3).

What was **not** optimized: topology operations, JSON parsing, validation, the event queue and the study scheduler showed no bottleneck worth the risk. Topology construction and neighbour queries are slightly slower (up to ~10% and ~19%, at 0.7-10 ms) as the cost of (1).

**Proof that behaviour did not change**
- The benchmark checksums over every path, metric, trace entry and study aggregate are **identical before and after in all runs** (C++: 15 benchmarks that compute results; the study checksum is also identical for 1, 2, 4, 8 and automatic workers). Re-verified in the Codespace; earlier on Windows (MSVC) with the quick workload (24 comparisons).
- The full pre-existing suite passes unchanged: 596 C++ tests (including the Phase 8 validation suite: exhaustive-enumeration and Bellman-Ford comparisons, shadow ledger, trace replay, determinism across worker counts, golden fixtures) and 102 Python tests.
- 10 new tests (`Optimization.*`): adjacency pointers belong to their own topology, equality ignores the pointer, copies survive destruction of the original, mutating a copy does not touch the original, copy assignment (incl. self-assignment), moves, `Network`-constructed topologies, 6,400-node and hop-limited searches that force the label table to grow, routing equality on a copy, and ascending `LinkId` order of allocations.

**Mutation testing of the optimizations** (production code deliberately broken, suite must fail, then restored byte-identical): 6 attempted, **4 detected, 2 equivalent mutants**.
- *Detected:* (a) `Topology` copy constructor copies the adjacency pointers (3 `Optimization` tests fail); (b) `Adjacency::operator==` also compares the pointer (`TopologyGraph.OutgoingExposesNeighborAndLinkSortedByLinkId` and an `Optimization` test fail); (c) `allocations()` without the sort (`Optimization.AllocationsAreReportedInAscendingLinkOrder` fails); (d) `addLink` indexes the caller's temporary copy (routing tests fail, e.g. `Routing.OneHopRoute`).
- *Equivalent (not caught, and cannot be):* (e) growing the label table at load factor 7/8 instead of 1/2: the table still always keeps at least one empty slot (it grows before the last slot fills), so every probe terminates and every lookup returns the same label; only probe length differs, which no functional test can observe. (f) Removing the `findLink` fallback for an adjacency entry with a null pointer: `Topology` is the only producer of `Adjacency` entries and always sets the pointer, so the fallback is unreachable through the public API (defensive code). No test was weakened to claim 6/6.

**Codespace findings (the first GCC build)** — the project had only been compiled with MSVC before, and `-Werror` on GCC 13 exposed: (1) ~35 `-Wdangling-else` errors in test code (`if (x) ASSERT_OK(...)`, because GoogleTest macros expand to `if/else`): fixed with braces; (2) a **latent test bug** found by `-Wdangling-reference`: `std::minmax(a.value(), b.value())` in `GeneratorTests.cpp` kept references to destroyed temporaries; fixed; (3) the static libraries were not position-independent, so linking them into the Python shared module failed on Linux: `CMAKE_POSITION_INDEPENDENT_CODE` is now set when `OPTICALNET_BUILD_PYTHON=ON`; (4) the Codespace's default git identity was not the project's, so the repository-local identity was set to `iamankoo` / `aniketraj00384@gmail.com`; (5) `ninja` had to be installed and Python 3.14 (default there) is newer than pybind11 2.13.6 supports, so Python 3.13.8 was used. No production-logic change was needed for GCC.

**Tests and builds (Codespace, GCC 13.3, clean build directories, warnings-as-errors, C++20)**
- Phase test counts: Phase 1 43, Phase 2 78, Phase 3 74, Phase 4 59, Phase 5 74, Phase 6 62, Phase 7 101, Phase 8 105, Phase 10 10 → **606 C++ tests** (501 unit + 105 validation). **102 Python tests**. **607 CTest entries**.
- **Debug 607/607 passed** (CTest wall time ≈ 44 s with `-j4` on this machine), **Release 607/607 passed** (≈ 5.5 s), re-run after the documentation changes on the final tree.
- On Windows/MSVC after the optimizations: the pre-existing suite passed on Release (597/597 CTest entries) and the 10 new tests passed; the full matrix was not repeated there.

**Final API validation.** The FastAPI integration tests (102) run against the real compiled extension under CTest on both Debug and Release; `examples/demo.py` was run end-to-end both in-process and against a real `uvicorn` server; `/health`, `/docs`, `/openapi.json` return 200; `bench_server.py` exercises the real server for `/route`, `/simulate`, `/study` and concurrent clients.

**Release / readiness work.** README rewritten with architecture, features, the 10 phases, measured benchmarks, test numbers, examples, exact Windows/VS Code and Codespaces commands and a 5-minute recruiter demo; `requirements.txt`, the CMake options, `examples/demo.py` and `benchmarks/` make a fresh clone reproducible. A release *tag* was not created (not requested); the final commit is the release candidate.

**Known limitations:** only one cloud VM (4 vCPU) was used, so absolute numbers vary by machine and thread scaling stops at 4 workers; the largest graph is 10,000 nodes (routing) and 400 nodes (simulation); topology construction/neighbour queries became slightly slower; no profiler/sanitizer/coverage run; memory is peak RSS only; the final matrix ran on Linux/GCC (Windows/MSVC and the PowerShell demo commands were not re-run in full after Phase 10; Clang untested); the Python extension is not packaged as a wheel; no authentication on the API.

**Commit:** `feat: complete OpticalNet optimization benchmarking and release` on `main` (the hash is in `git log`; a commit cannot contain its own hash).

**Final project status: OpticalNet — All 10 Phases COMPLETED.**


---

When a phase completes, its section is filled with: what was implemented, important classes/components, tests performed, build status, validation results, important decisions, commit hash, remaining limitations, next phase.
