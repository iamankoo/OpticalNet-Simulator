"""Shared fixtures. Nothing here mocks the C++ engine: every test runs the real pybind11 module."""
import copy
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from api import bootstrap  # noqa: E402,F401  (puts <repo>/python on sys.path; fails clearly if the extension is not built)
import opticalnet  # noqa: E402
from api.main import app  # noqa: E402
from api.store import store  # noqa: E402
from fastapi.testclient import TestClient  # noqa: E402


def link(id, source, target, length_km, capacity=2, direction=None, cost=None):
    entry = {"id": id, "source": source, "target": target, "length_km": length_km, "capacity_channels": capacity}
    if direction:
        entry["direction"] = direction
    if cost is not None:
        entry["cost"] = cost
    return entry


def nodes(*ids):
    return [{"id": i} for i in ids]


# 1 -- 2 -- 4 (short, 10 km links) and 1 -- 3 -- 4 (long, 20 km links); every link has 2 channels.
DIAMOND = {
    "nodes": nodes(1, 2, 3, 4),
    "links": [link(1, 1, 2, 10), link(2, 2, 4, 10), link(3, 1, 3, 20), link(4, 3, 4, 20)],
}
# Diamond plus an isolated node 5.
ISLAND = {"nodes": nodes(1, 2, 3, 4, 5), "links": DIAMOND["links"]}
# 1 -> 2 is one-way; 2 -- 3 is bidirectional.
DIRECTED = {"nodes": nodes(1, 2, 3), "links": [link(1, 1, 2, 5, direction="directed"), link(2, 2, 3, 5)]}
# Two parallel fibers between 1 and 2: link 1 is shorter than link 2? no - link 1 is 100 km / cost 5, link 2 is 200 km / cost 3.
PARALLEL = {"nodes": nodes(1, 2), "links": [link(1, 1, 2, 100, cost=5.0), link(2, 1, 2, 200, cost=3.0)]}
# 1 -- 2 -- 3 where link 1 has a single channel.
LINE = {"nodes": nodes(1, 2, 3), "links": [link(1, 1, 2, 10, capacity=1), link(2, 2, 3, 10, capacity=2)]}


@pytest.fixture
def client():
    store.clear()
    with TestClient(app) as c:
        yield c
    store.clear()


@pytest.fixture
def make_topology(client):
    """Submit a topology document through the API and return its id."""

    def _make(document):
        response = client.post("/topologies", json=copy.deepcopy(document))
        assert response.status_code == 201, response.text
        return response.json()["topology_id"]

    return _make


@pytest.fixture
def diamond(make_topology):
    return make_topology(DIAMOND)


@pytest.fixture
def engine_topology():
    """A C++ topology built directly through the Python binding (no HTTP)."""
    import json

    return opticalnet.topology_from_json(json.dumps(DIAMOND))
