"""OpticalNet Python API.

A thin layer over the C++ simulation engine (pybind11). Nothing here re-implements topology, routing, resource
allocation, simulation, failure/recovery, studies or validation: every call runs the C++ code and returns plain
Python values (ints, floats, strings, dicts, lists).

    import opticalnet
    topology = opticalnet.load_topology("configs/ring4.json")
    path = opticalnet.route(topology, source=1, destination=3, metric="distance")

Expected failures raise subclasses of :class:`OpticalNetError`; each carries a stable ``code`` string
(``NO_ROUTE``, ``NOT_FOUND``, ``INSUFFICIENT_CAPACITY`` ...).

Thread-safety: a :class:`Topology` is immutable and may be shared between threads. :class:`ConnectionSession` is
mutable and must be used by one caller at a time. Simulations and studies hold no global state and release the
GIL while the C++ engine runs.
"""
from ._opticalnet import (  # noqa: F401
    ConnectionSession,
    ConstraintViolationError,
    DuplicateIdError,
    InconsistentStateError,
    InsufficientCapacityError,
    InternalError,
    InvalidArgumentError,
    InvalidTransitionError,
    NoFeasibleRouteError,
    NoRouteError,
    NotFoundError,
    OpticalNetError,
    ParseError,
    RandomFailureConfig,
    ResourceUnavailableError,
    RoutingConstraints,
    ScriptedArrival,
    ScriptedFailure,
    SimulationConfig,
    Topology,
    load_topology,
    route,
    route_with,
    run_study,
    simulate,
    topology_from_json,
    validate_simulation,
    validate_topology,
)

__version__ = "0.9.0"

__all__ = [
    "ConnectionSession", "ConstraintViolationError", "DuplicateIdError", "InconsistentStateError",
    "InsufficientCapacityError", "InternalError", "InvalidArgumentError", "InvalidTransitionError",
    "NoFeasibleRouteError", "NoRouteError", "NotFoundError", "OpticalNetError", "ParseError",
    "RandomFailureConfig", "ResourceUnavailableError", "RoutingConstraints", "ScriptedArrival",
    "ScriptedFailure", "SimulationConfig", "Topology", "load_topology", "route", "route_with", "run_study",
    "simulate", "topology_from_json", "validate_simulation", "validate_topology",
]
