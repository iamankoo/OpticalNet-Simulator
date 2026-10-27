from fastapi import APIRouter

from .. import bootstrap  # noqa: F401
import opticalnet

from .. import convert, models

router = APIRouter(tags=["validation"])


@router.post("/validate", response_model=models.ValidateResponse, response_model_exclude_none=True,
             summary="Validate a topology (and optionally a simulation run)",
             description="Calls the C++ validation layer: topology rules and independent invariants and, if `simulation` is "
                         "given, a traced run checked by the independent trace validator. Nothing is validated in Python.",
             responses={404: {"model": models.ErrorResponse}, 422: {"model": models.ErrorResponse}})
def validate(request: models.ValidateRequest):
    topology = convert.resolve(request)
    report = opticalnet.validate_topology(topology)
    response = {"valid": report["valid"], "topology": report}
    if request.simulation is not None:
        check = opticalnet.validate_simulation(topology, convert.config(request.simulation.config),
                                               convert.arrivals(request.simulation.arrivals))
        response["simulation"] = {"passed": check["passed"], "failures": check["failures"],
                                  "trace_entries": check["trace_entries"]}
        response["valid"] = response["valid"] and check["passed"]
    return response
