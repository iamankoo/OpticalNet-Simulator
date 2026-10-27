"""/health, /docs, /topologies and /route over real HTTP-style requests into the C++ engine."""
import json
from pathlib import Path

import pytest

from api.store import TopologyStore, store
from conftest import DIAMOND, DIRECTED, ISLAND, PARALLEL, link, nodes

ROOT = Path(__file__).resolve().parents[2]


def error_of(response):
    return response.json()["error"]


# ---------------------------------------------------------------- health / docs
def test_health(client):
    response = client.get("/health")
    assert response.status_code == 200 and response.json() == {"status": "ok"}


def test_openapi_and_docs_are_served(client):
    assert client.get("/docs").status_code == 200
    spec = client.get("/openapi.json")
    assert spec.status_code == 200
    document = spec.json()
    for path in ("/health", "/topologies", "/route", "/simulate", "/study", "/validate", "/connections/scenario"):
        assert path in document["paths"], path
    assert {"RouteRequest", "SimulateRequest", "StudyRequest", "ErrorResponse"} <= set(document["components"]["schemas"])
    assert document["paths"]["/route"]["post"]["summary"]


# ---------------------------------------------------------------- topology
def test_valid_topology_is_accepted_and_summarised(client):
    response = client.post("/topologies", json=DIAMOND)
    assert response.status_code == 201
    body = response.json()
    assert (body["node_count"], body["link_count"]) == (4, 4)
    assert body["is_connected"] and body["is_strongly_connected"]
    assert body["validation"] == {"valid": True, "errors": 0, "warnings": 0}
    assert client.get(f"/topologies/{body['topology_id']}").json() == body
    assert body["topology_id"] in client.get("/topologies").json()["topologies"]


def test_the_shipped_ring4_config_is_accepted_unchanged(client):
    document = json.loads((ROOT / "configs" / "ring4.json").read_text())
    response = client.post("/topologies", json=document)
    assert response.status_code == 201 and response.json()["node_count"] == 4


def test_disconnected_topology_is_valid_with_a_warning(client):
    body = client.post("/topologies", json=ISLAND).json()
    assert body["is_connected"] is False
    assert body["validation"]["valid"] is True and body["validation"]["warnings"] >= 1


def test_nodes_and_links_are_listed(client, diamond):
    nodes_ = client.get(f"/topologies/{diamond}/nodes").json()["nodes"]
    assert [n["id"] for n in nodes_] == [1, 2, 3, 4]
    links_ = client.get(f"/topologies/{diamond}/links").json()["links"]
    assert [l["id"] for l in links_] == [1, 2, 3, 4]
    assert links_[2] == {"id": 3, "name": "link-3", "source": 1, "target": 3, "length_km": 20.0,
                         "attenuation_db_per_km": 0.2, "capacity_channels": 2, "direction": "bidirectional", "cost": 1.0}
    detail = client.get(f"/topologies/{diamond}/nodes/1").json()
    assert detail["neighbors"] == [2, 3] and detail["name"] == "node-1"
    assert client.get(f"/topologies/{diamond}/links/2").json()["target"] == 4


def test_export_round_trips_through_the_cpp_schema(client, diamond):
    exported = client.get(f"/topologies/{diamond}/export").json()
    again = client.post("/topologies", json=exported)
    assert again.status_code == 201
    assert client.get(f"/topologies/{again.json()['topology_id']}/links").json() == client.get(f"/topologies/{diamond}/links").json()


@pytest.mark.parametrize("document", [
    {"links": []},                                                   # missing required "nodes"
    {"nodes": [], "surprise": True},                                 # unknown top-level key (strict C++ schema)
    {"nodes": nodes(1, 2), "links": [{**link(1, 1, 2, 5), "colour": "red"}]},   # unknown link key
    {"nodes": nodes(1, 2), "links": [{"id": 1, "source": 1, "target": 2}]},     # missing required link fields
    {"nodes": [{"id": "one"}]},                                       # wrong type
])
def test_invalid_topology_documents_are_rejected_by_the_cpp_parser(client, document):
    response = client.post("/topologies", json=document)
    assert response.status_code == 422
    assert error_of(response)["code"] == "PARSE_ERROR"
    assert store.ids() == []  # nothing was stored


@pytest.mark.parametrize("document, code", [
    ({"nodes": nodes(1), "links": [link(1, 1, 2, 5)]}, "NOT_FOUND"),            # link to an unknown node
    ({"nodes": nodes(1, 1)}, "DUPLICATE_ID"),
    ({"nodes": nodes(1, 2), "links": [link(1, 1, 1, 5)]}, "INVALID_ARGUMENT"),   # self-link
    ({"nodes": nodes(1, 2), "links": [link(1, 1, 2, -5)]}, "INVALID_ARGUMENT"),  # negative length
])
def test_domain_rule_violations_keep_their_cpp_error_code(client, document, code):
    response = client.post("/topologies", json=document)
    assert response.status_code == 422 and error_of(response)["code"] == code


def test_unknown_topology_and_ids(client, diamond):
    assert client.get("/topologies/missing").status_code == 404
    assert error_of(client.get("/topologies/missing"))["code"] == "NOT_FOUND"
    assert client.get(f"/topologies/{diamond}/nodes/99").status_code == 404
    assert client.get(f"/topologies/{diamond}/links/99").status_code == 404
    assert client.get(f"/topologies/{diamond}/nodes/99999999999").status_code == 422


def test_delete_topology(client, diamond):
    assert client.delete(f"/topologies/{diamond}").status_code == 204
    assert client.get(f"/topologies/{diamond}").status_code == 404
    assert client.delete(f"/topologies/{diamond}").status_code == 404


def test_store_capacity_is_bounded(client, monkeypatch):
    monkeypatch.setattr("api.routes.topologies.store", TopologyStore(capacity=2))
    assert client.post("/topologies", json=DIAMOND).status_code == 201
    assert client.post("/topologies", json=DIAMOND).status_code == 201
    full = client.post("/topologies", json=DIAMOND)
    assert full.status_code == 409 and error_of(full)["code"] == "TOPOLOGY_STORE_FULL"


def test_oversized_body_is_rejected(client):
    response = client.post("/topologies", content=b'{"nodes": [], "pad": "' + b"a" * 8_100_000 + b'"}',
                           headers={"content-type": "application/json"})
    assert response.status_code == 413 and error_of(response)["code"] == "PAYLOAD_TOO_LARGE"


# ---------------------------------------------------------------- routing
def route(client, tid, source, destination, **extra):
    return client.post("/route", json={"topology_id": tid, "source": source, "destination": destination, **extra})


def test_valid_route_response_shape(client, diamond):
    response = route(client, diamond, 1, 4, metric="distance")
    assert response.status_code == 200
    assert response.json() == {"success": True, "path": {"nodes": [1, 2, 4], "links": [1, 2], "cost": 20.0,
                                                         "hop_count": 2, "metric": "distance"}}


def test_source_and_destination_are_not_swapped(client, make_topology):
    tid = make_topology(DIRECTED)
    assert route(client, tid, 1, 3).json()["path"]["nodes"] == [1, 2, 3]
    assert route(client, tid, 3, 1).status_code == 404  # the one-way link cannot be used backwards


def test_each_metric_is_mapped(client, make_topology):
    tid = make_topology(PARALLEL)
    assert route(client, tid, 1, 2, metric="administrative").json()["path"] == {
        "nodes": [1, 2], "links": [2], "cost": 3.0, "hop_count": 1, "metric": "administrative"}
    assert route(client, tid, 1, 2, metric="distance").json()["path"]["links"] == [1]
    assert route(client, tid, 1, 2, metric="hop_count").json()["path"]["cost"] == 1.0
    assert route(client, tid, 1, 2).json()["path"]["metric"] == "administrative"  # default


def test_parallel_links_report_which_fiber_is_used(client, make_topology):
    tid = make_topology(PARALLEL)
    assert route(client, tid, 1, 2, metric="distance", blocked_links=[1]).json()["path"]["links"] == [2]
    assert route(client, tid, 2, 1, metric="distance").json()["path"]["nodes"] == [2, 1]


def test_directed_links(client, make_topology):
    tid = make_topology(DIRECTED)
    assert route(client, tid, 3, 2).status_code == 200
    unreachable = route(client, tid, 2, 1)
    assert unreachable.status_code == 404 and error_of(unreachable)["code"] == "NO_ROUTE"


def test_constraints_are_applied(client, diamond):
    assert route(client, diamond, 1, 4, metric="distance", blocked_nodes=[2]).json()["path"]["nodes"] == [1, 3, 4]
    assert route(client, diamond, 1, 4, metric="distance", blocked_links=[2]).json()["path"]["links"] == [3, 4]
    assert route(client, diamond, 1, 4, metric="distance", max_cost=20).json()["path"]["cost"] == 20.0
    assert route(client, diamond, 1, 4, metric="distance", max_hops=2).status_code == 200
    for extra in ({"max_cost": 19.9}, {"max_hops": 1}, {"min_link_capacity": 3}, {"blocked_nodes": [2, 3]}):
        response = route(client, diamond, 1, 4, metric="distance", **extra)
        assert response.status_code == 404, extra
        assert error_of(response)["code"] == "NO_FEASIBLE_ROUTE", extra


def test_no_route_unknown_nodes_and_blocked_endpoints(client, make_topology):
    tid = make_topology(ISLAND)
    no_route = route(client, tid, 1, 5)
    assert no_route.status_code == 404 and error_of(no_route)["code"] == "NO_ROUTE"
    for source, destination in ((99, 1), (1, 99)):
        unknown = route(client, tid, source, destination)
        assert unknown.status_code == 404 and error_of(unknown)["code"] == "NOT_FOUND"
    assert error_of(route(client, tid, 1, 4, blocked_nodes=[1]))["code"] == "NO_FEASIBLE_ROUTE"


def test_inline_topology_is_parsed_by_cpp(client):
    response = client.post("/route", json={"topology": DIAMOND, "source": 1, "destination": 4, "metric": "distance"})
    assert response.status_code == 200 and response.json()["path"]["cost"] == 20.0
    bad = client.post("/route", json={"topology": {"nodes": [], "typo": 1}, "source": 1, "destination": 2})
    assert bad.status_code == 422 and error_of(bad)["code"] == "PARSE_ERROR"


def test_unknown_stored_topology_in_route(client):
    response = client.post("/route", json={"topology_id": "nope", "source": 1, "destination": 2})
    assert response.status_code == 404 and error_of(response)["code"] == "NOT_FOUND"
