from fastapi import APIRouter

from .. import bootstrap  # noqa: F401
import opticalnet

from .. import convert, models

router = APIRouter(tags=["routing"])


@router.post("/route", response_model=models.RouteResponse, summary="Compute a route",
             description="Runs the C++ Dijkstra router. Failures use stable codes: NOT_FOUND (unknown node), NO_ROUTE "
                         "(unreachable), NO_FEASIBLE_ROUTE (reachable, but not under the constraints).",
             responses={404: {"model": models.ErrorResponse}, 422: {"model": models.ErrorResponse}})
def route(request: models.RouteRequest):
    topology = convert.resolve(request)
    constraints = convert.routing(models.RoutingModel(
        metric=request.metric, max_cost=request.max_cost, max_hops=request.max_hops,
        min_link_capacity=request.min_link_capacity, blocked_nodes=request.blocked_nodes,
        blocked_links=request.blocked_links))
    path = opticalnet.route_with(topology, request.source, request.destination, constraints)
    return {"success": True, "path": path}
