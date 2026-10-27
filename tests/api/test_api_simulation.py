"""/simulate, failure/recovery scenarios, /study and /validate."""
import copy

import pytest

import opticalnet
from conftest import DIAMOND, ISLAND

WORKLOAD = {"seed": 7, "end_time": 150.0, "arrival_rate": 2.0, "mean_lifetime": 5.0, "min_demand": 1, "max_demand": 2,
            "routing": {"metric": "distance"}}


def simulate(client, tid, config=None, **extra):
    return client.post("/simulate", json={"topology_id": tid, "config": config or {}, **extra})


def metrics_of(response):
    assert response.status_code == 200, response.text
    return response.json()["metrics"]


# ---------------------------------------------------------------- generated workloads
def test_valid_result_and_accounting_identities(client, diamond):
    m = metrics_of(simulate(client, diamond, WORKLOAD))
    assert m["total_requests"] > 100
    assert m["accepted_requests"] + m["rejected_requests"] == m["total_requests"]
    assert m["rejected_requests"] == sum(m["rejections_by_reason"].values())
    assert m["acceptance_rate"] == pytest.approx(m["accepted_requests"] / m["total_requests"])
    assert m["acceptance_rate"] + m["blocking_rate"] == pytest.approx(1.0)
    assert m["total_capacity_channels"] == 8
    assert m["peak_allocated_channels"] <= m["total_capacity_channels"]
    assert m["final_allocated_channels"] + m["final_available_channels"] == m["total_capacity_channels"]
    assert 0.0 <= m["average_utilization"] <= 1.0
    assert m["total_failure_events"] == 0 and m["connections_lost"] == 0


def test_same_seed_gives_identical_json_and_other_seeds_differ(client, diamond):
    first = simulate(client, diamond, WORKLOAD).json()
    again = simulate(client, diamond, WORKLOAD).json()
    other = simulate(client, diamond, {**WORKLOAD, "seed": 8}).json()
    assert first == again
    assert first["seed"] == 7 and other["seed"] == 8
    assert first["metrics"] != other["metrics"]


def test_api_result_equals_the_direct_cpp_call(client, diamond):
    """The HTTP layer adds nothing and recomputes nothing: it returns exactly what the C++ engine produced."""
    via_http = metrics_of(simulate(client, diamond, WORKLOAD))
    topology = opticalnet.topology_from_json(__import__("json").dumps(DIAMOND))
    config = opticalnet.SimulationConfig()
    config.seed, config.end_time, config.arrival_rate, config.mean_lifetime = 7, 150.0, 2.0, 5.0
    config.max_demand = 2
    config.routing = opticalnet.RoutingConstraints(metric="distance")
    assert via_http == opticalnet.simulate(topology, config)["metrics"]


def test_max_requests_and_fixed_timing(client, diamond):
    m = metrics_of(simulate(client, diamond, {"max_requests": 5, "arrival_timing": "fixed", "lifetime_timing": "fixed",
                                              "arrival_rate": 1.0, "mean_lifetime": 2.0, "end_time": 100}))
    assert m["total_requests"] == 5 and m["accepted_requests"] == 5 and m["total_releases"] == 5
    assert metrics_of(simulate(client, diamond, {"max_requests": 0}))["total_requests"] == 0


def test_routing_constraints_reach_the_workload(client, diamond):
    base = {"seed": 3, "end_time": 60.0, "arrival_rate": 1.0, "mean_lifetime": 3.0}
    tight = metrics_of(simulate(client, diamond, {**base, "routing": {"metric": "hop_count", "max_hops": 1}}))
    assert tight["accepted_requests"] < tight["total_requests"]
    assert tight["rejections_by_reason"]["NO_FEASIBLE_ROUTE"] + tight["rejections_by_reason"].get("NO_ROUTE", 0) > 0
    loose = metrics_of(simulate(client, diamond, {**base, "routing": {"metric": "hop_count"}}))
    assert loose["accepted_requests"] > tight["accepted_requests"] or loose["accepted_requests"] == loose["total_requests"]


def test_trace_is_optional(client, diamond):
    config = {"max_requests": 3, "arrival_timing": "fixed", "lifetime_timing": "fixed", "mean_lifetime": 1.0}
    assert "trace" not in simulate(client, diamond, config).json()
    traced = simulate(client, diamond, config, include_trace=True).json()["trace"]
    assert [e["event"] for e in traced].count("connection_arrival") == 3
    assert all(e["success"] for e in traced)


def test_invalid_simulation_configurations_are_rejected(client, diamond):
    cases = [
        {"end_time": -1}, {"arrival_rate": -0.5}, {"mean_lifetime": 0}, {"min_demand": 0},
        {"min_demand": 3, "max_demand": 2}, {"arrival_timing": "poisson"}, {"recovery_policy": "magic"},
        {"seed": -1}, {"unknown_field": 1}, {"end_time": 1_000_000, "arrival_rate": 100},
        {"failure_schedule": [{"time": 1, "action": "fail_link", "id": 1}], "random_failures": {"failure_rate": 0.1}},
        {"random_failures": {"failure_rate": 0.1, "node_failure_probability": 2}},
        {"routing": {"metric": "latency"}}, {"routing": {"max_hops": -1}},
    ]
    for config in cases:
        response = simulate(client, diamond, config)
        assert response.status_code == 422, config
        assert response.json()["error"]["code"] == "VALIDATION_ERROR", config


def test_topology_reference_must_be_exactly_one(client, diamond):
    both = client.post("/simulate", json={"topology_id": diamond, "topology": DIAMOND})
    neither = client.post("/simulate", json={"config": {}})
    for response in (both, neither):
        assert response.status_code == 422 and response.json()["error"]["code"] == "VALIDATION_ERROR"
    assert client.post("/simulate", json={"topology_id": "nope"}).status_code == 404


# ---------------------------------------------------------------- failures
def arrival(time, id, lifetime=100.0, source=1, destination=4, demand=1):
    return {"time": time, "connection_id": id, "source": source, "destination": destination, "demand": demand,
            "lifetime": lifetime, "routing": {"metric": "distance"}}


def scripted(client, tid, arrivals, schedule, policy="reroute", end_time=50.0):
    return client.post("/simulate", json={
        "topology_id": tid, "arrivals": arrivals, "include_trace": True,
        "config": {"end_time": end_time, "failure_schedule": schedule, "recovery_policy": policy}})


def kinds(response):
    return [(e["kind"], e["event"], e["connection"]) for e in response.json()["trace"]]


def test_link_failure_reroutes_the_connection(client, diamond):
    response = scripted(client, diamond, [arrival(1, 1)], [{"time": 5, "action": "fail_link", "id": 1}])
    m = metrics_of(response)
    assert (m["link_failures"], m["connections_affected"], m["connections_rerouted"], m["connections_lost"]) == (1, 1, 1, 0)
    assert m["recovery_success_rate"] == 1.0 and m["total_connection_downtime"] == 0.0
    assert m["failed_links_at_end"] == 1 and m["accepted_requests"] == 1
    assert kinds(response) == [("event", "connection_arrival", 1), ("event", "link_failure", 0),
                               ("affected", "link_failure", 1), ("rerouted", "link_failure", 1)]


def test_node_failure_reroutes_around_the_node(client, diamond):
    m = metrics_of(scripted(client, diamond, [arrival(1, 1)], [{"time": 5, "action": "fail_node", "id": 2}]))
    assert (m["node_failures"], m["connections_rerouted"], m["failed_nodes_at_end"]) == (1, 1, 1)


def test_failure_without_recovery_policy_loses_the_connection(client, diamond):
    response = scripted(client, diamond, [arrival(1, 1)], [{"time": 5, "action": "fail_link", "id": 1}], policy="no_recovery")
    m = metrics_of(response)
    assert (m["connections_affected"], m["connections_rerouted"], m["connections_lost"]) == (1, 0, 1)
    assert m["recovery_success_rate"] == 0.0
    assert m["total_connection_downtime"] == 45.0          # would have lasted to the horizon (t=50) minus lost at t=5
    assert m["final_allocated_channels"] == 0              # a lost connection holds nothing
    assert kinds(response)[-1] == ("lost", "link_failure", 1)


def test_second_failure_can_make_a_rerouted_connection_lost(client, diamond):
    schedule = [{"time": 5, "action": "fail_link", "id": 1}, {"time": 6, "action": "fail_link", "id": 3}]
    m = metrics_of(scripted(client, diamond, [arrival(1, 1)], schedule))
    assert (m["connections_affected"], m["connections_rerouted"], m["connections_lost"]) == (2, 1, 1)
    assert m["recovery_success_rate"] == 0.5 and m["total_connection_downtime"] == 44.0
    assert m["final_allocated_channels"] == 0 and m["active_connections_at_end"] == 0


def test_recovery_restores_routing_for_new_requests(client, diamond):
    schedule = [{"time": 2, "action": "fail_link", "id": 1}, {"time": 4, "action": "recover_link", "id": 1}]
    arrivals = [arrival(3, 1, lifetime=5), arrival(5, 2, lifetime=5)]
    response = scripted(client, diamond, arrivals, schedule)
    m = metrics_of(response)
    assert (m["link_failures"], m["link_recoveries"], m["failed_links_at_end"]) == (1, 1, 0)
    assert m["accepted_requests"] == 2 and m["average_path_cost"] == 30.0   # (40 via the long branch + 20 via the short) / 2


def test_invalid_failure_schedules_are_rejected_by_the_engine(client, diamond):
    unknown = scripted(client, diamond, [arrival(1, 1)], [{"time": 5, "action": "fail_link", "id": 99}])
    assert unknown.status_code == 404 and unknown.json()["error"]["code"] == "NOT_FOUND"
    transition = scripted(client, diamond, [arrival(1, 1)], [{"time": 5, "action": "recover_link", "id": 1}])
    assert transition.status_code == 409 and transition.json()["error"]["code"] == "INVALID_TRANSITION"
    twice = scripted(client, diamond, [arrival(1, 1)], [{"time": 5, "action": "fail_node", "id": 2},
                                                          {"time": 6, "action": "fail_node", "id": 2}])
    assert twice.status_code == 409


def test_random_failures_are_deterministic_and_visible(client, diamond):
    config = {**WORKLOAD, "end_time": 200.0, "random_failures": {"failure_rate": 0.2, "mean_repair_time": 5.0,
                                                                 "node_failure_probability": 0.3}}
    first = simulate(client, diamond, config).json()["metrics"]
    assert first == simulate(client, diamond, config).json()["metrics"]
    assert first["total_failure_events"] > 10 and first["connections_affected"] > 0
    assert first["connections_rerouted"] + first["connections_lost"] == first["connections_affected"]
    no_recovery = simulate(client, diamond, {**config, "recovery_policy": "no_recovery"}).json()["metrics"]
    assert no_recovery["connections_rerouted"] == 0 and no_recovery["connections_lost"] == no_recovery["connections_affected"]
    plain = simulate(client, diamond, {**WORKLOAD, "end_time": 200.0}).json()["metrics"]
    assert plain["total_requests"] == first["total_requests"]    # failures never change the workload


# ---------------------------------------------------------------- study
def study(client, tid, **extra):
    body = {"topology_id": tid, "config": WORKLOAD, "replications": 6, "base_seed": 5, "workers": 2}
    body.update(extra)
    return client.post("/study", json=body)


def comparable(result):
    return {k: v for k, v in result.items() if k != "workers_used"}


def test_study_runs_replications_and_aggregates(client, diamond):
    response = study(client, diamond)
    assert response.status_code == 200, response.text
    body = response.json()
    assert body["base_seed"] == 5 and body["workers_used"] == 2
    assert [r["index"] for r in body["replications"]] == list(range(6))
    assert len({r["seed"] for r in body["replications"]}) == 6
    agg = body["aggregate"]
    assert agg["total_requests"] == sum(r["metrics"]["total_requests"] for r in body["replications"])
    assert agg["total_accepted"] == sum(r["metrics"]["accepted_requests"] for r in body["replications"])
    assert agg["aggregate_acceptance_rate"] == pytest.approx(agg["total_accepted"] / agg["total_requests"])
    stats = agg["acceptance_rate"]
    assert stats["count"] == 6 and stats["std_dev"] > 0 and stats["standard_error"] > 0
    low, high = stats["confidence_interval_95"]
    assert low < stats["mean"] < high
    assert low == pytest.approx(stats["mean"] - stats["confidence_half_width_95"])


def test_study_is_deterministic_and_independent_of_the_worker_count(client, diamond):
    results = {w: study(client, diamond, workers=w).json() for w in (1, 2, 4, 0)}
    for w in (2, 4, 0):
        assert comparable(results[w]) == comparable(results[1]), w
    assert study(client, diamond, workers=4).json() == results[4]
    assert comparable(study(client, diamond, base_seed=6).json()) != comparable(results[1])


def test_study_worker_count_is_passed_to_the_cpp_pool(client, diamond):
    assert study(client, diamond, replications=8, workers=3).json()["workers_used"] == 3
    assert study(client, diamond, replications=2, workers=8).json()["workers_used"] == 2   # never more threads than work
    assert study(client, diamond, replications=5, workers=1).json()["workers_used"] == 1
    assert study(client, diamond, replications=5, workers=0).json()["workers_used"] >= 1


def test_each_study_replication_equals_a_standalone_simulation(client, diamond):
    body = study(client, diamond, replications=3).json()
    for replication in body["replications"]:
        alone = simulate(client, diamond, {**WORKLOAD, "seed": replication["seed"]})
        assert metrics_of(alone) == replication["metrics"]


def test_study_with_failures_aggregates_recovery_figures(client, diamond):
    config = {**WORKLOAD, "random_failures": {"failure_rate": 0.1, "mean_repair_time": 4.0, "node_failure_probability": 0.2}}
    one = study(client, diamond, config=config, workers=1).json()
    four = study(client, diamond, config=config, workers=4).json()
    assert comparable(one) == comparable(four)
    agg = one["aggregate"]
    assert agg["total_failure_events"] > 0
    assert agg["total_connections_rerouted"] + agg["total_connections_lost"] == agg["total_connections_affected"]


def test_study_request_validation(client, diamond):
    for extra in ({"replications": 0}, {"replications": 10_001}, {"workers": -1}, {"workers": 65}, {"base_seed": -1},
                  {"replications": "many"}, {"config": {"end_time": -5}}):
        response = study(client, diamond, **extra)
        assert response.status_code == 422 and response.json()["error"]["code"] == "VALIDATION_ERROR", extra


# ---------------------------------------------------------------- validation endpoint
def test_validate_topology_and_simulation(client, diamond):
    body = client.post("/validate", json={"topology_id": diamond}).json()
    assert body["valid"] and body["topology"]["issues"] == [] and "simulation" not in body
    checked = client.post("/validate", json={"topology_id": diamond, "simulation": {"config": {
        **WORKLOAD, "end_time": 80.0, "random_failures": {"failure_rate": 0.2}}}}).json()
    assert checked["valid"] and checked["simulation"]["passed"] and checked["simulation"]["trace_entries"] > 0
    assert checked["simulation"]["failures"] == []


def test_validate_reports_warnings_and_rejects_bad_documents(client):
    island = client.post("/validate", json={"topology": ISLAND}).json()
    assert island["valid"] is True and island["topology"]["warnings"] >= 1
    assert island["topology"]["issues"][0]["severity"] == "warning"
    bad = client.post("/validate", json={"topology": {"nodes": [], "bogus": 1}})
    assert bad.status_code == 422 and bad.json()["error"]["code"] == "PARSE_ERROR"


def test_validate_scripted_run(client, diamond):
    body = {"topology_id": diamond, "simulation": {"arrivals": [arrival(1, 1)], "config": {
        "end_time": 50, "failure_schedule": [{"time": 5, "action": "fail_link", "id": 1}]}}}
    assert client.post("/validate", json=body).json()["simulation"]["passed"] is True
