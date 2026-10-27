"""Tests of the pybind11 module itself (no HTTP): it must be the real compiled C++ engine."""
import gc
import json
import threading
from concurrent.futures import ThreadPoolExecutor

import pytest

import opticalnet
from opticalnet import _opticalnet
from conftest import DIAMOND, ISLAND, LINE, PARALLEL, link, nodes


def topo(document):
    return opticalnet.topology_from_json(json.dumps(document))


def test_module_is_a_compiled_extension_not_a_python_imitation():
    path = getattr(_opticalnet, "__file__", "")
    assert path.endswith((".pyd", ".so")), path
    assert _opticalnet.Topology.__module__ == "opticalnet._opticalnet"
    # Python sources of the package contain no routing/resource algorithms: the functions are builtin (C++) callables.
    assert type(opticalnet.route).__name__ == "builtin_function_or_method"
    assert type(opticalnet.simulate).__name__ == "builtin_function_or_method"


def test_ring4_config_loads_from_file_and_routes():
    ring = opticalnet.load_topology("configs/ring4.json")
    assert (ring.node_count, ring.link_count) == (4, 4)
    path = opticalnet.route(ring, 1, 3, metric="distance")
    assert path == {"nodes": [1, 2, 3], "links": [1, 2], "cost": 300.0, "hop_count": 2, "metric": "distance"}


def test_topology_introspection():
    t = topo(DIAMOND)
    assert [n["id"] for n in t.nodes()] == [1, 2, 3, 4]
    assert [l["id"] for l in t.links()] == [1, 2, 3, 4]
    assert t.neighbors(1) == [2, 3]
    assert t.links_between(1, 2) == [1]
    assert t.link(3)["length_km"] == 20.0 and t.link(3)["direction"] == "bidirectional"
    assert t.is_connected() and t.is_strongly_connected()
    assert t.is_reachable(1, 4)
    assert t.connected_components() == [[1, 2, 3, 4]]
    assert not topo(ISLAND).is_connected()
    assert topo(ISLAND).connected_components() == [[1, 2, 3, 4], [5]]


def test_topology_json_round_trip_goes_through_cpp():
    t = topo(DIAMOND)
    again = opticalnet.topology_from_json(t.to_json())
    assert again.links() == t.links() and again.nodes() == t.nodes()


def test_routing_mapping_of_metric_and_constraints():
    t = topo(DIAMOND)
    assert opticalnet.route(t, 1, 4, metric="distance")["nodes"] == [1, 2, 4]
    assert opticalnet.route(t, 1, 4, metric="distance", blocked_nodes=[2])["nodes"] == [1, 3, 4]
    assert opticalnet.route(t, 1, 4, metric="distance", blocked_links=[1])["links"] == [3, 4]
    assert opticalnet.route(t, 1, 4, metric="distance", max_cost=20)["cost"] == 20.0
    assert opticalnet.route(t, 1, 4, metric="hop_count")["cost"] == 2.0
    parallel = topo(PARALLEL)
    assert opticalnet.route(parallel, 1, 2, metric="administrative")["links"] == [2]
    assert opticalnet.route(parallel, 1, 2, metric="distance")["links"] == [1]
    constraints = opticalnet.RoutingConstraints(metric="distance", max_hops=2, blocked_nodes=[3])
    assert (constraints.metric, constraints.max_hops, constraints.blocked_nodes) == ("distance", 2, [3])
    assert opticalnet.route_with(t, 1, 4, constraints)["nodes"] == [1, 2, 4]


@pytest.mark.parametrize("call, exception, code", [
    (lambda t: opticalnet.route(t, 1, 99), opticalnet.NotFoundError, "NOT_FOUND"),
    (lambda t: opticalnet.route(t, 1, 5), opticalnet.NoRouteError, "NO_ROUTE"),
    (lambda t: opticalnet.route(t, 1, 4, max_hops=1), opticalnet.NoFeasibleRouteError, "NO_FEASIBLE_ROUTE"),
    (lambda t: opticalnet.route(t, 1, 4, metric="bogus"), opticalnet.InvalidArgumentError, "INVALID_ARGUMENT"),
    (lambda t: opticalnet.route(t, 2**40, 4), None, None),
    (lambda t: opticalnet.route(t, -1, 4), opticalnet.InvalidArgumentError, "INVALID_ARGUMENT"),
])
def test_routing_errors_are_distinguishable(call, exception, code):
    t = topo(ISLAND)
    if exception is None:  # out of 64-bit range is rejected by the type conversion itself
        with pytest.raises((TypeError, OverflowError, opticalnet.OpticalNetError)):
            call(t)
        return
    with pytest.raises(exception) as info:
        call(t)
    assert info.value.code == code
    assert isinstance(info.value, opticalnet.OpticalNetError)


def test_every_cpp_error_code_has_a_python_exception_with_a_stable_code():
    classes = {name: getattr(_opticalnet, name) for name in dir(_opticalnet)
               if isinstance(getattr(_opticalnet, name), type) and issubclass(getattr(_opticalnet, name), opticalnet.OpticalNetError)
               and getattr(_opticalnet, name) is not opticalnet.OpticalNetError}
    codes = {cls.code for cls in classes.values()}
    assert codes == {"INVALID_ARGUMENT", "DUPLICATE_ID", "NOT_FOUND", "CONSTRAINT_VIOLATION", "PARSE_ERROR", "NO_ROUTE",
                     "NO_FEASIBLE_ROUTE", "INSUFFICIENT_CAPACITY", "INCONSISTENT_STATE", "INTERNAL_ERROR",
                     "INVALID_TRANSITION", "RESOURCE_UNAVAILABLE"}
    assert len(classes) == 12


def test_parse_errors_come_from_the_cpp_schema():
    with pytest.raises(opticalnet.ParseError):
        opticalnet.topology_from_json("{not json")
    with pytest.raises(opticalnet.ParseError):
        opticalnet.topology_from_json(json.dumps({"nodes": [], "surprise": 1}))
    with pytest.raises(opticalnet.NotFoundError):
        opticalnet.topology_from_json(json.dumps({"nodes": nodes(1), "links": [link(1, 1, 2, 5)]}))
    with pytest.raises(opticalnet.DuplicateIdError):
        opticalnet.topology_from_json(json.dumps({"nodes": nodes(1, 1)}))


def test_connection_session_lifecycle_and_errors():
    s = opticalnet.ConnectionSession(topo(LINE))
    c1 = s.establish(1, 1, 3, 1)
    assert c1["path"]["links"] == [1, 2] and c1["demand"] == 1
    assert s.usage(1) == {"link": 1, "total": 1, "allocated": 1, "available": 0}
    with pytest.raises(opticalnet.InsufficientCapacityError):
        s.establish(2, 1, 3, 1)
    with pytest.raises(opticalnet.DuplicateIdError):
        s.establish(1, 1, 3, 1)
    with pytest.raises(opticalnet.InvalidArgumentError):
        s.establish(3, 1, 1, 1)
    with pytest.raises(opticalnet.NotFoundError):
        s.release(99)
    assert s.totals() == {"capacity": 3, "allocated": 2, "available": 1, "active_connections": 1}
    s.release(1)
    assert s.totals()["allocated"] == 0 and s.active_count == 0
    with pytest.raises(opticalnet.NotFoundError):
        s.release(1)  # second release
    assert s.validate()["valid"]


def test_failure_overlay_blocks_routing_without_touching_the_topology():
    s = opticalnet.ConnectionSession(topo(DIAMOND))
    s.fail_link(1)
    assert s.establish(1, 1, 4, 1)["path"]["links"] == [3, 4]
    s.fail_node(3)
    with pytest.raises(opticalnet.OpticalNetError) as info:
        s.establish(2, 1, 4, 1)
    assert info.value.code in {"NO_ROUTE", "NO_FEASIBLE_ROUTE"}
    with pytest.raises(opticalnet.InvalidTransitionError):
        s.fail_link(1)
    s.recover_link(1)
    s.recover_node(3)
    with pytest.raises(opticalnet.InvalidTransitionError):
        s.recover_node(3)
    assert s.failed_links() == [] and s.failed_nodes() == []


def test_session_keeps_its_topology_alive():
    t = topo(DIAMOND)
    s = opticalnet.ConnectionSession(t)
    del t
    gc.collect()
    assert s.establish(1, 1, 4, 1)["path"]["nodes"] == [1, 2, 4]
    path = s.connection(1)["path"]
    del s
    gc.collect()
    assert path["nodes"] == [1, 2, 4]  # results are plain Python values, independent of any C++ object


def test_simulation_config_properties_and_validation():
    c = opticalnet.SimulationConfig()
    c.seed, c.end_time, c.arrival_rate = 5, 30.0, 2.0
    c.arrival_timing = "fixed"
    assert (c.seed, c.end_time, c.arrival_timing) == (5, 30.0, "fixed")
    with pytest.raises(opticalnet.InvalidArgumentError):
        c.recovery_policy = "bogus"
    c.routing.max_hops = 3  # nested object is live, not a copy
    assert c.routing.max_hops == 3
    c.arrival_rate = -1.0
    with pytest.raises(opticalnet.InvalidArgumentError):
        opticalnet.simulate(topo(DIAMOND), c)


def test_simulation_is_deterministic_and_seed_sensitive():
    t = topo(DIAMOND)

    def run(seed):
        c = opticalnet.SimulationConfig()
        c.seed, c.end_time, c.arrival_rate, c.mean_lifetime = seed, 100.0, 3.0, 5.0
        c.max_demand = 2
        return opticalnet.simulate(t, c)

    assert run(11) == run(11)
    assert run(11)["metrics"] != run(12)["metrics"]
    assert run(11)["seed"] == 11


def test_study_matches_standalone_runs_for_each_derived_seed():
    t = topo(DIAMOND)
    c = opticalnet.SimulationConfig()
    c.end_time, c.arrival_rate, c.mean_lifetime = 60.0, 2.0, 4.0
    study = opticalnet.run_study(t, c, 5, 3, 2)
    assert [r["index"] for r in study["replications"]] == [0, 1, 2, 3, 4]
    assert len({r["seed"] for r in study["replications"]}) == 5
    for r in study["replications"]:
        c.seed = r["seed"]
        assert opticalnet.simulate(t, c)["metrics"] == r["metrics"]
    assert study["workers_used"] == 2


def test_study_rejects_bad_arguments():
    t, c = topo(DIAMOND), opticalnet.SimulationConfig()
    with pytest.raises(opticalnet.InvalidArgumentError):
        opticalnet.run_study(t, c, 0)
    with pytest.raises(opticalnet.InvalidArgumentError):
        opticalnet.run_study(t, c, 3, 1, -1)


def test_simulations_run_concurrently_with_the_gil_released():
    t = topo(DIAMOND)

    def run(seed):
        c = opticalnet.SimulationConfig()
        c.seed, c.end_time, c.arrival_rate = seed, 200.0, 4.0
        return opticalnet.simulate(t, c)["metrics"]

    expected = [run(s) for s in range(1, 9)]
    with ThreadPoolExecutor(8) as pool:
        got = list(pool.map(run, range(1, 9)))
    assert got == expected  # a shared Topology is safe to read from many threads

    # a long-running C++ call must not block other Python threads (proves the GIL is released)
    ticks = []
    stop = threading.Event()

    def ticker():
        while not stop.is_set():
            ticks.append(1)
            stop.wait(0.001)

    big = opticalnet.SimulationConfig()
    big.end_time, big.arrival_rate = 30000.0, 20.0
    thread = threading.Thread(target=ticker)
    thread.start()
    try:
        opticalnet.simulate(t, big)
    finally:
        stop.set()
        thread.join()
    assert len(ticks) > 5


def test_validators_are_the_cpp_ones():
    assert opticalnet.validate_topology(topo(DIAMOND))["valid"]
    island = opticalnet.validate_topology(topo(ISLAND))
    assert island["valid"] and island["warnings"] >= 1
    c = opticalnet.SimulationConfig()
    c.end_time, c.arrival_rate = 50.0, 2.0
    c.random_failures = opticalnet.RandomFailureConfig(0.2, 5.0, 0.3)
    result = opticalnet.validate_simulation(topo(DIAMOND), c)
    assert result["passed"] and result["trace_entries"] > 0
