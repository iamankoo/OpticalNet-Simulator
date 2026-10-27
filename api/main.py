"""FastAPI application. Run with:  uvicorn api.main:app --reload

Architecture:  HTTP -> FastAPI -> Pydantic models -> `opticalnet` Python package -> pybind11 -> C++ engine.
This module only wires routers; there is no simulation logic here.
"""
from . import bootstrap  # noqa: F401
import opticalnet

from fastapi import FastAPI, Request
from fastapi.responses import JSONResponse

from . import errors
from .routes import connections, health, routing, simulation, topologies, validation

MAX_BODY_BYTES = 8_000_000

app = FastAPI(
    title="OpticalNet API",
    version=opticalnet.__version__,
    description="Local HTTP interface to the OpticalNet C++ optical-network simulator. All topology, routing, resource, "
                "simulation, failure/recovery, study and validation logic runs in the C++ engine.",
)
errors.install(app)


@app.middleware("http")
async def limit_body_size(request: Request, call_next):
    length = request.headers.get("content-length")
    if length is not None and length.isdigit() and int(length) > MAX_BODY_BYTES:
        return JSONResponse(status_code=413, content=errors.error_body("PAYLOAD_TOO_LARGE", "request body is too large"))
    return await call_next(request)


for module in (health, topologies, routing, connections, simulation, validation):
    app.include_router(module.router)
