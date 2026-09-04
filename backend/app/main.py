from __future__ import annotations

import logging
from collections.abc import AsyncIterator
from contextlib import asynccontextmanager

from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware

from app.api.routes import router
from app.config.settings import Settings, get_settings
from app.container import bootstrap_application
from app.services.build_info import load_build_info
from app.services.ports import SimulationRunner

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s %(levelname)s %(name)s %(message)s",
)


def create_app(
    settings: Settings | None = None,
    runner_override: SimulationRunner | None = None,
) -> FastAPI:
    app_settings = settings or get_settings()
    build_info = load_build_info(app_settings)

    @asynccontextmanager
    async def lifespan(app: FastAPI) -> AsyncIterator[None]:
        runtime = await bootstrap_application(
            app, app_settings, build_info, runner_override
        )
        yield
        await runtime.shutdown()

    app = FastAPI(title="JSB1", version="0.1.0", lifespan=lifespan)
    app.add_middleware(
        CORSMiddleware,
        allow_origins=app_settings.cors_origins,
        allow_credentials=False,
        allow_methods=["GET", "POST", "DELETE"],
        allow_headers=["Content-Type"],
    )
    app.include_router(router)
    return app


app = create_app()
