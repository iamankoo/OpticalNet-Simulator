"""Pydantic request/response models: the public API surface (not a mirror of the C++ classes).

They check shape and obvious ranges only. Domain correctness (does the node exist? is the schedule valid? ...) is
decided by the C++ engine and reported through its stable error codes.
"""
from __future__ import annotations

from typing import Annotated, Any, Literal, Optional, Union

from pydantic import BaseModel, ConfigDict, Field, model_validator

Id = Annotated[int, Field(ge=0, le=2**32 - 1, description="Unsigned 32-bit identifier")]
Demand = Annotated[int, Field(ge=1, le=2**32 - 1, description="Channels needed on every link of the path")]
Metric = Literal["hop_count", "distance", "administrative"]
Timing = Literal["exponential", "fixed"]
RecoveryPolicy = Literal["reroute", "no_recovery"]

# Bounds that keep a single local request from running for an unreasonable time.
MAX_END_TIME = 1_000_000.0
MAX_EXPECTED_REQUESTS = 1_000_000
MAX_REPLICATIONS = 10_000
MAX_WORKERS = 64
MAX_SCRIPT_ITEMS = 10_000


class Strict(BaseModel):
    model_config = ConfigDict(extra="forbid")


# ---------------------------------------------------------------- errors
class ErrorInfo(BaseModel):
    code: str = Field(description="Stable error code, e.g. NO_ROUTE, NOT_FOUND, INSUFFICIENT_CAPACITY, VALIDATION_ERROR")
    message: str
    details: Optional[Any] = None


class ErrorResponse(BaseModel):
    error: ErrorInfo


# ---------------------------------------------------------------- topology
class TopologyRef(Strict):
    """Either a stored topology (`topology_id`) or an inline topology document (parsed by the C++ schema)."""

    topology_id: Optional[str] = Field(default=None, description="Id returned by POST /topologies")
    topology: Optional[dict[str, Any]] = Field(
        default=None, description="Inline topology document (same schema as POST /topologies)")

    @model_validator(mode="after")
    def _exactly_one(self):
        if (self.topology_id is None) == (self.topology is None):
            raise ValueError("provide exactly one of 'topology_id' or 'topology'")
        return self


class ValidationSummary(BaseModel):
    valid: bool
    errors: int
    warnings: int


class TopologySummary(BaseModel):
    topology_id: str
    node_count: int
    link_count: int
    is_connected: bool
    is_strongly_connected: bool
    validation: ValidationSummary


class TopologyList(BaseModel):
    topologies: list[str]


class NodeInfo(BaseModel):
    id: int
    name: str
    switching_element: Optional[int]
    transceivers: list[int]


class NodeDetail(NodeInfo):
    neighbors: list[int]


class LinkInfo(BaseModel):
    id: int
    name: str
    source: int
    target: int
    length_km: float
    attenuation_db_per_km: float
    capacity_channels: int
    direction: Literal["bidirectional", "directed"]
    cost: float


class NodeList(BaseModel):
    nodes: list[NodeInfo]


class LinkList(BaseModel):
    links: list[LinkInfo]


# ---------------------------------------------------------------- routing
class RoutingModel(Strict):
    metric: Metric = "administrative"
    max_cost: Optional[float] = Field(default=None, ge=0, allow_inf_nan=False)
    max_hops: Optional[Id] = None
    min_link_capacity: Id = 0
    blocked_nodes: list[Id] = Field(default_factory=list, max_length=MAX_SCRIPT_ITEMS)
    blocked_links: list[Id] = Field(default_factory=list, max_length=MAX_SCRIPT_ITEMS)


class RouteRequest(TopologyRef, RoutingModel):
    source: Id
    destination: Id


class PathModel(BaseModel):
    nodes: list[int]
    links: list[int]
    cost: float
    hop_count: int
    metric: Metric


class RouteResponse(BaseModel):
    success: Literal[True] = True
    path: PathModel


# ---------------------------------------------------------------- connections
class EstablishOp(Strict):
    op: Literal["establish"]
    id: Id
    source: Id
    destination: Id
    demand: Demand = 1
    routing: Optional[RoutingModel] = None


class ReleaseOp(Strict):
    op: Literal["release"]
    id: Id


class ResourceOp(Strict):
    op: Literal["fail_link", "recover_link", "fail_node", "recover_node"]
    id: Id


Operation = Annotated[Union[EstablishOp, ReleaseOp, ResourceOp], Field(discriminator="op")]


class ScenarioRequest(TopologyRef):
    operations: list[Operation] = Field(max_length=MAX_SCRIPT_ITEMS)


class ConnectionModel(BaseModel):
    id: int
    source: int
    destination: int
    demand: int
    path: PathModel


class StepError(BaseModel):
    code: str
    message: str


class StepResult(BaseModel):
    index: int
    op: str
    ok: bool
    connection: Optional[ConnectionModel] = None
    error: Optional[StepError] = None


class LinkUsageModel(BaseModel):
    link: int
    total: int
    allocated: int
    available: int


class ResourceTotals(BaseModel):
    capacity: int
    allocated: int
    available: int
    active_connections: int


class IssueModel(BaseModel):
    severity: str
    code: str
    message: str


class InvariantFailure(BaseModel):
    check: str
    message: str


class StateValidation(BaseModel):
    valid: bool
    issues: list[IssueModel]
    invariant_failures: list[InvariantFailure]


class FinalState(BaseModel):
    totals: ResourceTotals
    usages: list[LinkUsageModel]
    connections: list[ConnectionModel]
    failed_links: list[int]
    failed_nodes: list[int]
    validation: StateValidation


class ScenarioResponse(BaseModel):
    steps: list[StepResult]
    final: FinalState


# ---------------------------------------------------------------- simulation
class FailureEntry(Strict):
    time: float = Field(ge=0, le=MAX_END_TIME, allow_inf_nan=False)
    action: Literal["fail_link", "recover_link", "fail_node", "recover_node"]
    id: Id


class RandomFailureModel(Strict):
    failure_rate: float = Field(ge=0, le=1000, allow_inf_nan=False)
    mean_repair_time: float = Field(default=10.0, gt=0, allow_inf_nan=False)
    node_failure_probability: float = Field(default=0.0, ge=0, le=1)


class SimulationConfigModel(Strict):
    seed: int = Field(default=1, ge=0, lt=2**64)
    end_time: float = Field(default=100.0, ge=0, le=MAX_END_TIME, allow_inf_nan=False)
    max_requests: Optional[int] = Field(default=None, ge=0, lt=2**64)
    arrival_rate: float = Field(default=1.0, ge=0, le=100_000, allow_inf_nan=False)
    arrival_timing: Timing = "exponential"
    mean_lifetime: float = Field(default=10.0, gt=0, allow_inf_nan=False)
    lifetime_timing: Timing = "exponential"
    min_demand: Demand = 1
    max_demand: Demand = 1
    routing: RoutingModel = Field(default_factory=RoutingModel)
    failure_schedule: list[FailureEntry] = Field(default_factory=list, max_length=MAX_SCRIPT_ITEMS)
    random_failures: Optional[RandomFailureModel] = None
    recovery_policy: RecoveryPolicy = "reroute"

    @model_validator(mode="after")
    def _combinations(self):
        if self.min_demand > self.max_demand:
            raise ValueError("min_demand must not exceed max_demand")
        if self.failure_schedule and self.random_failures is not None:
            raise ValueError("use either failure_schedule or random_failures, not both")
        if self.end_time * self.arrival_rate > MAX_EXPECTED_REQUESTS:
            raise ValueError(f"end_time * arrival_rate must not exceed {MAX_EXPECTED_REQUESTS} expected requests")
        return self


class ArrivalModel(Strict):
    time: float = Field(ge=0, le=MAX_END_TIME, allow_inf_nan=False)
    connection_id: Id
    source: Id
    destination: Id
    demand: Demand = 1
    lifetime: float = Field(gt=0, allow_inf_nan=False)
    routing: Optional[RoutingModel] = None


class SimulateRequest(TopologyRef):
    config: SimulationConfigModel = Field(default_factory=SimulationConfigModel)
    arrivals: Optional[list[ArrivalModel]] = Field(
        default=None, max_length=MAX_SCRIPT_ITEMS,
        description="Optional explicit workload (scripted run). When given, the generated-workload settings are ignored.")
    include_trace: bool = False


class MetricsModel(BaseModel):
    total_requests: int
    accepted_requests: int
    rejected_requests: int
    rejections_by_reason: dict[str, int]
    acceptance_rate: float
    blocking_rate: float
    total_capacity_channels: int
    peak_allocated_channels: int
    average_utilization: float
    final_allocated_channels: int
    final_available_channels: int
    average_path_cost: float
    average_hop_count: float
    max_hop_count: int
    active_connections_at_end: int
    peak_active_connections: int
    total_releases: int
    total_failure_events: int
    link_failures: int
    node_failures: int
    link_recoveries: int
    node_recoveries: int
    connections_affected: int
    connections_rerouted: int
    connections_lost: int
    recovery_success_rate: float
    total_connection_downtime: float
    failed_links_at_end: int
    failed_nodes_at_end: int
    events_processed: int
    duration: float


class TraceEntryModel(BaseModel):
    time: float
    event: str
    kind: str
    connection: int
    source: int
    destination: int
    demand: int
    success: bool
    reason: Optional[str]
    hops: int
    resource: int


class SimulateResponse(BaseModel):
    seed: int
    metrics: MetricsModel
    trace: Optional[list[TraceEntryModel]] = None


# ---------------------------------------------------------------- study
class StudyRequest(TopologyRef):
    config: SimulationConfigModel = Field(default_factory=SimulationConfigModel)
    replications: int = Field(default=10, ge=1, le=MAX_REPLICATIONS)
    base_seed: int = Field(default=1, ge=0, lt=2**64)
    workers: int = Field(default=0, ge=0, le=MAX_WORKERS, description="0 = automatic")

    @model_validator(mode="after")
    def _total_work(self):
        if self.config.end_time * self.config.arrival_rate * self.replications > 10 * MAX_EXPECTED_REQUESTS:
            raise ValueError("replications * end_time * arrival_rate is too large for a single request")
        return self


class SampleStats(BaseModel):
    count: int
    mean: float
    std_dev: float
    standard_error: float
    confidence_half_width_95: float
    confidence_interval_95: tuple[float, float]


class ReplicationModel(BaseModel):
    index: int
    seed: int
    metrics: MetricsModel


class AggregateModel(BaseModel):
    total_requests: int
    total_accepted: int
    total_rejected: int
    total_releases: int
    rejections_by_reason: dict[str, int]
    total_failure_events: int
    total_connections_affected: int
    total_connections_rerouted: int
    total_connections_lost: int
    aggregate_recovery_success_rate: float
    total_connection_downtime: float
    aggregate_acceptance_rate: float
    aggregate_blocking_rate: float
    mean_active_connections_at_end: float
    mean_peak_active_connections: float
    mean_peak_allocated_channels: float
    mean_final_allocated_channels: float
    mean_average_utilization: float
    average_path_cost: float
    average_hop_count: float
    acceptance_rate: SampleStats
    blocking_rate: SampleStats
    average_utilization: SampleStats


class StudyResponse(BaseModel):
    base_seed: int
    workers_used: int
    replications: list[ReplicationModel]
    aggregate: AggregateModel


# ---------------------------------------------------------------- validation
class SimulationCheck(Strict):
    config: SimulationConfigModel = Field(default_factory=SimulationConfigModel)
    arrivals: Optional[list[ArrivalModel]] = Field(default=None, max_length=MAX_SCRIPT_ITEMS)


class ValidateRequest(TopologyRef):
    simulation: Optional[SimulationCheck] = Field(
        default=None, description="If given, run this simulation with a trace and check it with the C++ trace validator.")


class TopologyValidation(BaseModel):
    valid: bool
    errors: int
    warnings: int
    issues: list[IssueModel]
    invariant_failures: list[InvariantFailure]


class SimulationValidation(BaseModel):
    passed: bool
    failures: list[InvariantFailure]
    trace_entries: int


class ValidateResponse(BaseModel):
    valid: bool
    topology: TopologyValidation
    simulation: Optional[SimulationValidation] = None
