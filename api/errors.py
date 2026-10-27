"""Stable error codes and the mapping from C++ errors to HTTP responses."""
from __future__ import annotations

import logging
from typing import Any

from fastapi import FastAPI, Request
from fastapi.exceptions import RequestValidationError
from fastapi.responses import JSONResponse

from . import bootstrap  # noqa: F401
import opticalnet

log = logging.getLogger("opticalnet.api")

# C++ ErrorCode (as exposed by the binding's `code` attribute) -> HTTP status.
STATUS_BY_CODE = {
    "INVALID_ARGUMENT": 422,
    "PARSE_ERROR": 422,
    "NOT_FOUND": 404,
    "NO_ROUTE": 404,
    "NO_FEASIBLE_ROUTE": 404,
    "DUPLICATE_ID": 409,
    "CONSTRAINT_VIOLATION": 409,
    "INSUFFICIENT_CAPACITY": 409,
    "INVALID_TRANSITION": 409,
    "RESOURCE_UNAVAILABLE": 409,
    "INCONSISTENT_STATE": 500,
    "INTERNAL_ERROR": 500,
}


class ApiError(Exception):
    """An error raised by the API layer itself (not by the C++ engine)."""

    def __init__(self, status: int, code: str, message: str, details: Any = None):
        super().__init__(message)
        self.status, self.code, self.message, self.details = status, code, message, details


def error_body(code: str, message: str, details: Any = None) -> dict:
    error: dict[str, Any] = {"code": code, "message": message}
    if details is not None:
        error["details"] = details
    return {"error": error}


def describe(exc: opticalnet.OpticalNetError) -> tuple[int, str, str]:
    """(http status, stable code, message) for an engine error."""
    code = getattr(exc, "code", "INTERNAL_ERROR")
    return STATUS_BY_CODE.get(code, 500), code, str(exc)


def install(app: FastAPI) -> None:
    @app.exception_handler(ApiError)
    async def _api_error(_: Request, exc: ApiError):
        return JSONResponse(status_code=exc.status, content=error_body(exc.code, exc.message, exc.details))

    @app.exception_handler(opticalnet.OpticalNetError)
    async def _engine_error(_: Request, exc: opticalnet.OpticalNetError):
        status, code, message = describe(exc)
        return JSONResponse(status_code=status, content=error_body(code, message))

    @app.exception_handler(RequestValidationError)
    async def _validation_error(_: Request, exc: RequestValidationError):
        details = [{"location": [str(p) for p in e.get("loc", ())], "message": e.get("msg", "")} for e in exc.errors()]
        return JSONResponse(status_code=422, content=error_body("VALIDATION_ERROR", "request validation failed", details))

    @app.exception_handler(Exception)
    async def _unexpected(_: Request, exc: Exception):
        log.exception("unexpected error")  # the details stay in the server log, not in the response
        return JSONResponse(status_code=500, content=error_body("INTERNAL_ERROR", "unexpected internal error"))
