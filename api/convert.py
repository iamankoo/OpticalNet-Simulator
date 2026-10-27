"""Pydantic models -> engine objects. Pure translation: no validation of domain rules, no algorithms."""
from __future__ import annotations

import json

from . import bootstrap  # noqa: F401
import opticalnet

from . import models
from .errors import ApiError
from .store import store

MAX_TOPOLOGY_JSON_BYTES = 4_000_000


def routing(m: models.RoutingModel | None) -> opticalnet.RoutingConstraints:
    m = m or models.RoutingModel()
    return opticalnet.RoutingConstraints(
        metric=m.metric, max_cost=m.max_cost, max_hops=m.max_hops, min_link_capacity=m.min_link_capacity,
        blocked_nodes=m.blocked_nodes, blocked_links=m.blocked_links)


def config(m: models.SimulationConfigModel) -> opticalnet.SimulationConfig:
    c = opticalnet.SimulationConfig()
    c.seed = m.seed
    c.end_time = m.end_time
    c.max_requests = m.max_requests
    c.arrival_rate = m.arrival_rate
    c.arrival_timing = m.arrival_timing
    c.mean_lifetime = m.mean_lifetime
    c.lifetime_timing = m.lifetime_timing
    c.min_demand = m.min_demand
    c.max_demand = m.max_demand
    c.routing = routing(m.routing)
    c.failure_schedule = [opticalnet.ScriptedFailure(f.time, f.action, f.id) for f in m.failure_schedule]
    if m.random_failures is not None:
        r = m.random_failures
        c.random_failures = opticalnet.RandomFailureConfig(r.failure_rate, r.mean_repair_time, r.node_failure_probability)
    c.recovery_policy = m.recovery_policy
    return c


def arrivals(items: list[models.ArrivalModel] | None):
    if items is None:
        return None
    return [opticalnet.ScriptedArrival(a.time, a.connection_id, a.source, a.destination, a.demand, a.lifetime,
                                       routing(a.routing) if a.routing else None) for a in items]


def parse_topology(document: dict) -> opticalnet.Topology:
    """Pass the document to the C++ parser; the C++ schema (including unknown-key rejection) is authoritative."""
    text = json.dumps(document, allow_nan=False)
    if len(text) > MAX_TOPOLOGY_JSON_BYTES:
        raise ApiError(413, "PAYLOAD_TOO_LARGE", f"topology document exceeds {MAX_TOPOLOGY_JSON_BYTES} bytes")
    try:
        return opticalnet.topology_from_json(text)
    except opticalnet.OpticalNetError as exc:
        # The fault is in the submitted document, so it is a 422 whatever the engine's code (which is kept).
        raise ApiError(422, exc.code, str(exc)) from exc


def resolve(ref: models.TopologyRef) -> opticalnet.Topology:
    if ref.topology_id is not None:
        return store.get(ref.topology_id)
    return parse_topology(ref.topology)
