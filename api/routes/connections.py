from fastapi import APIRouter

from .. import bootstrap  # noqa: F401
import opticalnet

from .. import convert, models
from ..errors import describe

router = APIRouter(tags=["connections"])


def _apply(session: opticalnet.ConnectionSession, op: models.Operation, step: dict) -> None:
    if op.op == "establish":
        step["connection"] = session.establish(op.id, op.source, op.destination, op.demand, convert.routing(op.routing))
    elif op.op == "release":
        session.release(op.id)
    elif op.op == "fail_link":
        session.fail_link(op.id)
    elif op.op == "recover_link":
        session.recover_link(op.id)
    elif op.op == "fail_node":
        session.fail_node(op.id)
    else:
        session.recover_node(op.id)


@router.post("/connections/scenario", response_model=models.ScenarioResponse, summary="Run a connection/resource scenario",
             description="Creates a request-owned C++ ConnectionManager over the topology, applies the operations in order "
                         "(establish, release, fail/recover link or node) and returns every step's outcome plus the final "
                         "resource state and its C++ consistency validation. Nothing persists after the response. A failing "
                         "step is reported in `steps` (with its stable error code) and does not stop later steps.",
             responses={404: {"model": models.ErrorResponse}, 422: {"model": models.ErrorResponse}})
def scenario(request: models.ScenarioRequest):
    topology = convert.resolve(request)
    session = opticalnet.ConnectionSession(topology)
    steps = []
    for index, op in enumerate(request.operations):
        step = {"index": index, "op": op.op, "ok": True, "connection": None, "error": None}
        try:
            _apply(session, op, step)
        except opticalnet.OpticalNetError as exc:
            _, code, message = describe(exc)
            step.update(ok=False, error={"code": code, "message": message})
        steps.append(step)
    final = {
        "totals": session.totals(), "usages": session.usages(), "connections": session.connections(),
        "failed_links": session.failed_links(), "failed_nodes": session.failed_nodes(), "validation": session.validate(),
    }
    return {"steps": steps, "final": final}
