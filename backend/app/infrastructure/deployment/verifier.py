from __future__ import annotations

import asyncio
import http.client
import socket
import ssl
import time
import urllib.error
import urllib.request
from collections.abc import Callable

from app.config.settings import Settings
from app.domain.deployment import BranchDeployment


class DeploymentVerificationError(RuntimeError):
    pass


class DeploymentVerifier:
    """Own deployment port probes and HTTP/TLS health verification."""

    def __init__(self, settings: Settings) -> None:
        self.settings = settings

    async def wait_for_http(self, deployment: BranchDeployment) -> None:
        if deployment.frontend_port is None:
            raise DeploymentVerificationError(
                "deployment frontend port is not allocated"
            )
        url = (
            f"http://{self.settings.deployment_health_host}:"
            f"{deployment.frontend_port}/api/health"
        )
        await self.wait_until_healthy(
            lambda: self.http_ok(url), "application health check"
        )

    async def wait_for_https(self, hostname: str) -> None:
        await self.wait_until_healthy(
            lambda: self.https_ok(hostname), "HTTPS route health check"
        )

    async def wait_until_healthy(
        self, check: Callable[[], bool], label: str
    ) -> None:
        deadline = time.monotonic() + self.settings.deployment_health_timeout_sec
        while time.monotonic() < deadline:
            if await asyncio.to_thread(check):
                return
            await asyncio.sleep(self.settings.deployment_health_interval_sec)
        raise DeploymentVerificationError(f"{label} timed out")

    @staticmethod
    def http_ok(url: str) -> bool:
        try:
            with urllib.request.urlopen(url, timeout=3) as response:
                return response.status == 200
        except (OSError, urllib.error.URLError):
            return False

    def https_ok(self, hostname: str) -> bool:
        context = ssl._create_unverified_context()
        connection: socket.socket | ssl.SSLSocket | None = None
        try:
            connection = socket.create_connection(
                (self.settings.caddy_health_host, self.settings.deployment_https_port),
                timeout=3,
            )
            connection = context.wrap_socket(connection, server_hostname=hostname)
            request = (
                f"GET /api/health HTTP/1.1\r\nHost: {hostname}\r\n"
                "Connection: close\r\n\r\n"
            )
            connection.sendall(request.encode("ascii"))
            response = connection.recv(64)
            return response.startswith((b"HTTP/1.1 200", b"HTTP/2 200"))
        except (OSError, ssl.SSLError, http.client.HTTPException):
            return False
        finally:
            if connection is not None:
                connection.close()

    @staticmethod
    def assert_ports_free(deployment: BranchDeployment) -> None:
        for port in (deployment.frontend_port, deployment.backend_port):
            if port is None:
                raise DeploymentVerificationError(
                    "deployment port is not allocated"
                )
            with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
                probe.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
                try:
                    probe.bind(("127.0.0.1", port))
                except OSError as exc:
                    raise DeploymentVerificationError(
                        f"allocated deployment port {port} is already in use"
                    ) from exc

    def occupied_ports(self) -> set[int]:
        occupied: set[int] = set()
        host = self.settings.deployment_port_probe_host
        for port in range(
            self.settings.deployment_port_start,
            self.settings.deployment_port_end + 1,
        ):
            if host in ("127.0.0.1", "localhost"):
                with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
                    try:
                        probe.bind(("127.0.0.1", port))
                    except OSError:
                        occupied.add(port)
                continue
            try:
                with socket.create_connection((host, port), timeout=0.01):
                    occupied.add(port)
            except OSError:
                pass
        return occupied
