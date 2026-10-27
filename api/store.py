"""In-memory registry of immutable topologies.

Only *immutable* C++ topologies are shared between requests. Connection managers, failure state and simulations are
created per request and destroyed with it, so the store is the only shared object and a single lock protects it.
"""
from __future__ import annotations

import secrets
import threading

from . import bootstrap  # noqa: F401
import opticalnet

from .errors import ApiError

MAX_TOPOLOGIES = 64


class TopologyStore:
    def __init__(self, capacity: int = MAX_TOPOLOGIES):
        self._capacity = capacity
        self._lock = threading.Lock()
        self._items: dict[str, opticalnet.Topology] = {}

    def add(self, topology: opticalnet.Topology) -> str:
        with self._lock:
            if len(self._items) >= self._capacity:
                raise ApiError(409, "TOPOLOGY_STORE_FULL", f"at most {self._capacity} topologies can be stored; delete one first")
            topology_id = secrets.token_hex(6)
            self._items[topology_id] = topology
            return topology_id

    def get(self, topology_id: str) -> opticalnet.Topology:
        with self._lock:
            topology = self._items.get(topology_id)
        if topology is None:
            raise ApiError(404, "NOT_FOUND", f"topology '{topology_id}' does not exist")
        return topology

    def remove(self, topology_id: str) -> None:
        with self._lock:
            if self._items.pop(topology_id, None) is None:
                raise ApiError(404, "NOT_FOUND", f"topology '{topology_id}' does not exist")

    def ids(self) -> list[str]:
        with self._lock:
            return list(self._items)

    def clear(self) -> None:
        with self._lock:
            self._items.clear()


store = TopologyStore()
