from fastapi import APIRouter

from .. import bootstrap  # noqa: F401
import opticalnet

from .. import convert, models

router = APIRouter(tags=["simulation"])
_ERRORS = {404: {"model": models.ErrorResponse}, 422: {"model": models.ErrorResponse}}


@router.post("/simulate", response_model=models.SimulateResponse, response_model_exclude_none=True,
             summary="Run one simulation",
             description="Runs the C++ discrete-event simulation (generated workload, or `arrivals` for a scripted one), "
                         "including scripted or random failures and the recovery policy. Deterministic for a given seed. "
                         "The GIL is released while the engine runs.",
             responses=_ERRORS)
def simulate(request: models.SimulateRequest):
    topology = convert.resolve(request)
    config = convert.config(request.config)
    config.record_trace = request.include_trace
    result = opticalnet.simulate(topology, config, convert.arrivals(request.arrivals))
    if not request.include_trace:
        result.pop("trace", None)
    return result


@router.post("/study", response_model=models.StudyResponse, summary="Run a multithreaded simulation study",
             description="Independent replications run on the C++ thread pool (`workers`, 0 = automatic). Results are ordered "
                         "by replication index and do not depend on the worker count. The aggregate carries means, "
                         "standard deviation, standard error and a 95% confidence interval.",
             responses=_ERRORS)
def study(request: models.StudyRequest):
    topology = convert.resolve(request)
    config = convert.config(request.config)
    result = opticalnet.run_study(topology, config, request.replications, request.base_seed, request.workers)
    for replication in result["replications"]:
        replication.pop("trace", None)
    return result
