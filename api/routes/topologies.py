import json
from typing import Any

from fastapi import APIRouter, Body, Response

from .. import bootstrap  # noqa: F401
import opticalnet

from .. import convert, models
from ..errors import ApiError
from ..store import store

router = APIRouter(prefix="/topologies", tags=["topology"])

_ERRORS = {404: {"model": models.ErrorResponse}, 422: {"model": models.ErrorResponse}}
_EXAMPLE = {"nodes": [{"id": 1}, {"id": 2}],
            "links": [{"id": 1, "source": 1, "target": 2, "length_km": 100, "capacity_channels": 8}]}


def _summary(topology_id: str, topology: opticalnet.Topology) -> models.TopologySummary:
    report = opticalnet.validate_topology(topology)
    return models.TopologySummary(
        topology_id=topology_id, node_count=topology.node_count, link_count=topology.link_count,
        is_connected=topology.is_connected(), is_strongly_connected=topology.is_strongly_connected(),
        validation=models.ValidationSummary(valid=report["valid"], errors=report["errors"], warnings=report["warnings"]))


def _check_id(value: int, what: str) -> None:
    if not 0 <= value <= 2**32 - 1:
        raise ApiError(422, "INVALID_ARGUMENT", f"{what} is outside the valid id range")


@router.post("", status_code=201, response_model=models.TopologySummary, summary="Submit a topology",
             description="Body: a topology document in the OpticalNet JSON schema. It is parsed by the C++ engine, which "
                         "rejects malformed documents and unknown keys.",
             responses={413: {"model": models.ErrorResponse}, 422: {"model": models.ErrorResponse}})
def create_topology(document: dict[str, Any] = Body(..., examples=[_EXAMPLE])):
    topology = convert.parse_topology(document)
    return _summary(store.add(topology), topology)


@router.get("", response_model=models.TopologyList, summary="List stored topology ids")
def list_topologies():
    return {"topologies": store.ids()}


@router.get("/{topology_id}", response_model=models.TopologySummary, summary="Inspect a topology", responses=_ERRORS)
def get_topology(topology_id: str):
    return _summary(topology_id, store.get(topology_id))


@router.delete("/{topology_id}", status_code=204, summary="Delete a stored topology", responses=_ERRORS)
def delete_topology(topology_id: str):
    store.remove(topology_id)
    return Response(status_code=204)


@router.get("/{topology_id}/export", summary="Export the topology as JSON (OpticalNet schema)", responses=_ERRORS)
def export_topology(topology_id: str) -> dict:
    return json.loads(store.get(topology_id).to_json())


@router.get("/{topology_id}/nodes", response_model=models.NodeList, summary="List nodes", responses=_ERRORS)
def nodes(topology_id: str):
    return {"nodes": store.get(topology_id).nodes()}


@router.get("/{topology_id}/nodes/{node_id}", response_model=models.NodeDetail, summary="Node details and neighbors",
            responses=_ERRORS)
def node(topology_id: str, node_id: int):
    topology = store.get(topology_id)
    _check_id(node_id, "node id")
    info = topology.node(node_id)
    info["neighbors"] = topology.neighbors(node_id)
    return info


@router.get("/{topology_id}/links", response_model=models.LinkList, summary="List links", responses=_ERRORS)
def links(topology_id: str):
    return {"links": store.get(topology_id).links()}


@router.get("/{topology_id}/links/{link_id}", response_model=models.LinkInfo, summary="Link details", responses=_ERRORS)
def link(topology_id: str, link_id: int):
    topology = store.get(topology_id)
    _check_id(link_id, "link id")
    return topology.link(link_id)
