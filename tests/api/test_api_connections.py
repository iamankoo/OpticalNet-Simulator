"""/connections/scenario: resource and connection management through the C++ ConnectionManager."""
from conftest import DIAMOND, LINE


def run(client, tid, operations):
    response = client.post("/connections/scenario", json={"topology_id": tid, "operations": operations})
    assert response.status_code == 200, response.text
    return response.json()


def establish(id, source, destination, demand=1, **extra):
    return {"op": "establish", "id": id, "source": source, "destination": destination, "demand": demand, **extra}


def test_successful_connection_allocates_capacity(client, make_topology):
    body = run(client, make_topology(LINE), [establish(1, 1, 3, 1)])
    step = body["steps"][0]
    assert step["ok"] and step["error"] is None
    assert step["connection"]["path"]["links"] == [1, 2] and step["connection"]["demand"] == 1
    final = body["final"]
    assert final["totals"] == {"capacity": 3, "allocated": 2, "available": 1, "active_connections": 1}
    assert final["usages"] == [{"link": 1, "total": 1, "allocated": 1, "available": 0},
                               {"link": 2, "total": 2, "allocated": 1, "available": 1}]
    assert [c["id"] for c in final["connections"]] == [1]
    assert final["validation"] == {"valid": True, "issues": [], "invariant_failures": []}


def test_capacity_rejection_then_release_frees_capacity(client, make_topology):
    body = run(client, make_topology(LINE), [
        establish(1, 1, 3), establish(2, 1, 3), {"op": "release", "id": 1}, establish(2, 1, 3)])
    ok = [s["ok"] for s in body["steps"]]
    assert ok == [True, False, True, True]
    assert body["steps"][1]["error"]["code"] == "INSUFFICIENT_CAPACITY"
    assert body["final"]["totals"]["allocated"] == 2 and body["final"]["totals"]["active_connections"] == 1
    assert body["final"]["validation"]["valid"]


def test_failed_operation_changes_nothing(client, make_topology):
    tid = make_topology(LINE)
    before = run(client, tid, [establish(1, 1, 3)])["final"]
    after = run(client, tid, [establish(1, 1, 3), establish(2, 1, 3)])["final"]
    assert after["totals"] == before["totals"] and after["usages"] == before["usages"]


def test_invalid_connection_operations_report_stable_codes(client, make_topology):
    body = run(client, make_topology(LINE), [
        establish(1, 1, 3),
        establish(1, 2, 3),                 # duplicate id
        {"op": "release", "id": 42},        # unknown connection
        {"op": "release", "id": 1},
        {"op": "release", "id": 1},         # second release
        establish(2, 1, 1),                 # source == destination
        establish(3, 1, 99),                # unknown node
        {"op": "fail_link", "id": 99},      # unknown link
        {"op": "recover_link", "id": 1},    # not failed
    ])
    codes = [s["error"]["code"] if s["error"] else None for s in body["steps"]]
    assert codes == [None, "DUPLICATE_ID", "NOT_FOUND", None, "NOT_FOUND", "INVALID_ARGUMENT", "NOT_FOUND", "NOT_FOUND",
                     "INVALID_TRANSITION"]
    assert body["final"]["totals"]["allocated"] == 0


def test_routing_constraints_apply_to_establish(client, diamond):
    body = run(client, diamond, [
        establish(1, 1, 4, 1, routing={"metric": "distance"}),
        establish(2, 1, 4, 1, routing={"metric": "distance", "max_hops": 1}),
        establish(3, 1, 4, 1, routing={"metric": "distance", "blocked_nodes": [2]}),
    ])
    assert body["steps"][0]["connection"]["path"]["nodes"] == [1, 2, 4]
    assert body["steps"][1]["error"]["code"] == "NO_FEASIBLE_ROUTE"
    assert body["steps"][2]["connection"]["path"]["nodes"] == [1, 3, 4]


def test_capacity_aware_routing_uses_the_other_branch(client, diamond):
    body = run(client, diamond, [establish(i, 1, 4, 1, routing={"metric": "distance"}) for i in (1, 2, 3, 4, 5)])
    paths = [s["connection"]["path"]["nodes"] if s["ok"] else s["error"]["code"] for s in body["steps"]]
    assert paths[:2] == [[1, 2, 4], [1, 2, 4]]       # the short branch (2 channels) fills first
    assert paths[2:4] == [[1, 3, 4], [1, 3, 4]]      # then traffic is routed around the full links
    assert paths[4] == "INSUFFICIENT_CAPACITY" or paths[4] == "NO_FEASIBLE_ROUTE" or paths[4] == "NO_ROUTE"
    assert body["final"]["totals"]["allocated"] == 8 and body["final"]["validation"]["valid"]


def test_failures_in_a_scenario_steer_routing(client, diamond):
    body = run(client, diamond, [
        {"op": "fail_link", "id": 1}, establish(1, 1, 4, 1, routing={"metric": "distance"}),
        {"op": "recover_link", "id": 1}, establish(2, 1, 4, 1, routing={"metric": "distance"}),
        {"op": "fail_node", "id": 3}])
    assert body["steps"][1]["connection"]["path"]["links"] == [3, 4]
    assert body["steps"][3]["connection"]["path"]["links"] == [1, 2]
    assert body["final"]["failed_nodes"] == [3] and body["final"]["failed_links"] == []


def test_scenario_state_is_per_request(client, make_topology):
    tid = make_topology(LINE)
    first = run(client, tid, [establish(1, 1, 3)])
    second = run(client, tid, [establish(1, 1, 3)])  # same id again: would be DUPLICATE_ID if state leaked
    assert first == second and second["steps"][0]["ok"]


def test_scenario_request_validation(client, diamond):
    bad = [{"op": "establish", "id": 1, "source": 1, "destination": 2, "demand": 0},
           {"op": "teleport", "id": 1}, {"op": "release"}, {"op": "release", "id": -1}]
    for op in bad:
        response = client.post("/connections/scenario", json={"topology_id": diamond, "operations": [op]})
        assert response.status_code == 422 and response.json()["error"]["code"] == "VALIDATION_ERROR", op
