"""Error mapping and the end-to-end chain HTTP -> FastAPI -> Pydantic -> binding -> C++ -> JSON."""
import json
import threading
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import pytest
from fastapi.testclient import TestClient

import opticalnet
from api import errors
from api.main import app
from conftest import DIAMOND

ROOT = Path(__file__).resolve().parents[2]


def test_every_cpp_error_code_maps_to_a_stable_http_status():
    engine_codes = {getattr(opticalnet._opticalnet, n).code for n in dir(opticalnet._opticalnet)
                    if isinstance(getattr(opticalnet._opticalnet, n), type)
                    and issubclass(getattr(opticalnet._opticalnet, n), opticalnet.OpticalNetError)
                    and getattr(opticalnet._opticalnet, n) is not opticalnet.OpticalNetError}
    assert engine_codes == set(errors.STATUS_BY_CODE)
    assert errors.STATUS_BY_CODE["NOT_FOUND"] == 404 and errors.STATUS_BY_CODE["NO_ROUTE"] == 404
    assert errors.STATUS_BY_CODE["INSUFFICIENT_CAPACITY"] == 409 and errors.STATUS_BY_CODE["INVALID_ARGUMENT"] == 422


@pytest.mark.parametrize("exception, status, code", [
    (opticalnet.NotFoundError, 404, "NOT_FOUND"),
    (opticalnet.NoRouteError, 404, "NO_ROUTE"),
    (opticalnet.NoFeasibleRouteError, 404, "NO_FEASIBLE_ROUTE"),
    (opticalnet.InvalidArgumentError, 422, "INVALID_ARGUMENT"),
    (opticalnet.ParseError, 422, "PARSE_ERROR"),
    (opticalnet.DuplicateIdError, 409, "DUPLICATE_ID"),
    (opticalnet.InsufficientCapacityError, 409, "INSUFFICIENT_CAPACITY"),
    (opticalnet.InvalidTransitionError, 409, "INVALID_TRANSITION"),
    (opticalnet.ResourceUnavailableError, 409, "RESOURCE_UNAVAILABLE"),
    (opticalnet.ConstraintViolationError, 409, "CONSTRAINT_VIOLATION"),
    (opticalnet.InconsistentStateError, 500, "INCONSISTENT_STATE"),
    (opticalnet.InternalError, 500, "INTERNAL_ERROR"),
])
def test_engine_exceptions_become_stable_error_responses(monkeypatch, exception, status, code):
    def boom(*args, **kwargs):
        raise exception("engine said no")

    monkeypatch.setattr(opticalnet, "route_with", boom)
    with TestClient(app) as client:
        response = client.post("/route", json={"topology": DIAMOND, "source": 1, "destination": 2})
    assert response.status_code == status
    assert response.json() == {"error": {"code": code, "message": "engine said no"}}


def test_unexpected_exceptions_do_not_leak_details(monkeypatch):
    def boom(*args, **kwargs):
        raise RuntimeError("secret internal detail 0xDEADBEEF")

    monkeypatch.setattr(opticalnet, "route_with", boom)
    with TestClient(app, raise_server_exceptions=False) as client:
        response = client.post("/route", json={"topology": DIAMOND, "source": 1, "destination": 2})
    assert response.status_code == 500
    assert response.json() == {"error": {"code": "INTERNAL_ERROR", "message": "unexpected internal error"}}
    assert "DEADBEEF" not in response.text


def test_real_engine_errors_end_to_end(client):
    cases = [
        ({"source": 1, "destination": 99}, 404, "NOT_FOUND"),
        ({"source": 1, "destination": 4, "max_hops": 1}, 404, "NO_FEASIBLE_ROUTE"),
        ({"source": -1, "destination": 4}, 422, "VALIDATION_ERROR"),
        ({"source": 1, "destination": 4, "metric": "bogus"}, 422, "VALIDATION_ERROR"),
        ({"source": 1}, 422, "VALIDATION_ERROR"),
    ]
    for body, status, code in cases:
        response = client.post("/route", json={"topology": DIAMOND, **body})
        assert response.status_code == status and response.json()["error"]["code"] == code, body
    assert client.post("/route", content="not json", headers={"content-type": "application/json"}).status_code == 422
    assert client.get("/no/such/endpoint").status_code == 404


def test_validation_errors_point_at_the_field(client):
    response = client.post("/route", json={"topology": DIAMOND, "source": 1, "destination": -2})
    error = response.json()["error"]
    assert error["code"] == "VALIDATION_ERROR"
    assert any(d["location"][-1] == "destination" for d in error["details"])


def test_end_to_end_http_to_cpp_and_back(client):
    """HTTP request -> FastAPI -> Pydantic -> pybind11 -> real C++ OpticalNet -> JSON, with no mocks anywhere."""
    ring = json.loads((ROOT / "configs" / "ring4.json").read_text())
    created = client.post("/topologies", json=ring)
    assert created.status_code == 201
    tid = created.json()["topology_id"]

    # the response must have been produced by the compiled extension
    assert opticalnet._opticalnet.__file__.endswith((".pyd", ".so"))

    # routing: known hand-computed answer for the 4-node ring (150 km spans)
    routed = client.post("/route", json={"topology_id": tid, "source": 1, "destination": 3, "metric": "distance"})
    assert routed.status_code == 200
    assert routed.json() == {"success": True, "path": {"nodes": [1, 2, 3], "links": [1, 2], "cost": 300.0, "hop_count": 2,
                                                       "metric": "distance"}}

    # resources
    scenario = client.post("/connections/scenario", json={"topology_id": tid, "operations": [
        {"op": "establish", "id": 1, "source": 1, "destination": 3, "demand": 2}]}).json()
    assert scenario["final"]["totals"]["allocated"] == 4 and scenario["final"]["validation"]["valid"]

    # simulation with failures, then the same simulation validated independently inside C++
    request = {"topology_id": tid, "config": {"seed": 21, "end_time": 120, "arrival_rate": 1.5, "mean_lifetime": 8,
                                              "random_failures": {"failure_rate": 0.1, "mean_repair_time": 6}}}
    simulated = client.post("/simulate", json=request)
    assert simulated.status_code == 200
    metrics = simulated.json()["metrics"]
    assert metrics["total_requests"] > 0 and metrics["accepted_requests"] + metrics["rejected_requests"] == metrics["total_requests"]
    checked = client.post("/validate", json={"topology_id": tid, "simulation": {"config": request["config"]}}).json()
    assert checked["valid"] and checked["simulation"]["passed"]
    assert checked["topology"]["valid"]

    # the C++ study, run twice with different worker counts
    study = {"topology_id": tid, "config": request["config"], "replications": 8, "base_seed": 3}
    a = client.post("/study", json={**study, "workers": 1}).json()
    b = client.post("/study", json={**study, "workers": 4}).json()
    assert a["replications"] == b["replications"] and a["aggregate"] == b["aggregate"]
    assert (a["workers_used"], b["workers_used"]) == (1, 4)


def test_concurrent_requests_do_not_share_mutable_state(client):
    """Many threads issue the same scenario/simulation; each request owns its C++ state."""
    tid = client.post("/topologies", json=DIAMOND).json()["topology_id"]
    scenario = {"topology_id": tid, "operations": [
        {"op": "establish", "id": 1, "source": 1, "destination": 4, "demand": 2, "routing": {"metric": "distance"}}]}
    sim = {"topology_id": tid, "config": {"seed": 4, "end_time": 120, "arrival_rate": 2, "max_demand": 2}}
    expected_scenario = client.post("/connections/scenario", json=scenario).json()
    expected_sim = client.post("/simulate", json=sim).json()
    barrier = threading.Barrier(8)

    def work(i):
        barrier.wait()
        with TestClient(app) as c:
            if i % 2:
                return c.post("/connections/scenario", json=scenario).json() == expected_scenario
            return c.post("/simulate", json=sim).json() == expected_sim

    with ThreadPoolExecutor(8) as pool:
        assert all(pool.map(work, range(8)))
