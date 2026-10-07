# OpticalNet — Project Summary (living document)

Updated after each completed phase. Nothing is recorded here as done unless it has actually been implemented and verified.

## Project overview

OpticalNet is an optical network simulation and validation platform that models network topology, routing, resource allocation, concurrent simulation, failures/recovery, and network validation. Core in C++20; Python/FastAPI integration layer. Repository: https://github.com/iamankoo/OpticalNet-Simulator

Source-of-truth documents: [Phases.md](Phases.md), [ARCHITECTURE.md](ARCHITECTURE.md), [TECHSTACK.md](TECHSTACK.md).

## Current status

- **Implementation status:** Phases 1 (core domain model, build/test foundation), 2 (topology & graph engine), 3 (routing engine), 4 (resource & connection management) and 5 (simulation engine) are implemented and tested. Phases 6–10 are not started.
- **Current phase:** Phase 5 complete; ready for Phase 6
- **Completed phases:** 1, 2, 3, 4, 5
- **In-progress phase:** None
- **Pending phases:** 6–10
- **Key implemented components:** `Network`, `Node`, `FiberLink`, `Transceiver`, `SwitchingElement`, `INetworkElement`, strong IDs, `Result<T>`/`Error`, `Topology`, `Adjacency`, `ValidationReport`, `CostMetric`/`linkCost`, JSON topology loader/exporter, `Path`, `RoutingConstraints`, `IRoutingAlgorithm`, `DijkstraRouter`, `RoutingEngine`, `ConnectionRequest`, `Connection`, `ConnectionManager`, `ResourceManager`, `LinkUsage`, `SimulationEngine`, `SimulationConfig`, `RequestGenerator`, `EventQueue`, `Rng`, `SimulationResult`/`SimulationMetrics`
- **Current tests/status:** 328 GoogleTest cases (43 Phase 1 + 78 Phase 2 + 74 Phase 3 + 59 Phase 4 + 74 Phase 5), all passing under CTest in Release and Debug (MSVC, warnings-as-errors)
- **Known limitations:** See the Phase 1–5 summaries. No multithreaded simulation, concurrency, failures, validation framework, or Python layer yet.
- **Next phase:** Phase 6 — Multithreaded Simulation

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
- Single-threaded; not safe for concurrent use until Phase 6.
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
- Single-threaded (Phase 6 adds concurrent workers); no failure/recovery events (Phase 7); no benchmarking (Phase 10).
- Only Poisson (or fixed) arrivals and exponential (or fixed) lifetimes, uniform endpoint selection and uniform demand: no traffic matrices, hotspots or other distributions.
- No warm-up period: metrics include the initial transient from an empty network.
- No confidence intervals or multi-run aggregation.
- Utilization counts channel-links over total channels, so long routes weigh more; it is not a per-link average.
- Exact random sequences are implementation-specific across standard libraries.
- Times are `double`, so equal-time ordering relies on exactly equal values (e.g. fixed timings that are exactly representable).
- Not tested with GCC/Clang.

**Commit:** `feat: implement OpticalNet phase 5 simulation engine` on `main` (the hash is in `git log`; a commit cannot contain its own hash).

**Next phase:** Phase 6 — Multithreaded Simulation

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
