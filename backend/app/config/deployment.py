from __future__ import annotations

import re

from app.config.settings import PROJECT_ROOT, Settings
from app.domain.errors import DeploymentConfigurationError
from app.infrastructure.deployment.tls import validate_tls_paths


class DeploymentConfigurationValidator:
    """Validate deployment settings at the configuration boundary."""

    def __init__(self, settings: Settings) -> None:
        self.settings = settings

    def validate(self) -> None:
        domain = self.settings.deployment_base_domain.lower().rstrip(".")
        main_hostname = self.settings.deployment_main_hostname.lower().rstrip(".")
        if not re.fullmatch(
            r"(?=.{1,253}$)(?:[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?\.)+"
            r"[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?",
            domain,
        ):
            raise DeploymentConfigurationError("invalid JSB1_DEPLOYMENT_BASE_DOMAIN")
        if not re.fullmatch(
            r"[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?\." + re.escape(domain),
            main_hostname,
        ):
            raise DeploymentConfigurationError(
                "JSB1_DEPLOYMENT_MAIN_HOSTNAME must be exactly one label beneath the base domain"
            )
        if self.settings.deployment_port_start >= self.settings.deployment_port_end:
            raise DeploymentConfigurationError(
                "JSB1_DEPLOYMENT_PORT_START must be lower than JSB1_DEPLOYMENT_PORT_END"
            )
        for value, name in (
            (self.settings.deployment_health_host, "JSB1_DEPLOYMENT_HEALTH_HOST"),
            (
                self.settings.deployment_port_probe_host,
                "JSB1_DEPLOYMENT_PORT_PROBE_HOST",
            ),
            (self.settings.caddy_upstream_host, "JSB1_CADDY_UPSTREAM_HOST"),
            (self.settings.caddy_health_host, "JSB1_CADDY_HEALTH_HOST"),
        ):
            if not re.fullmatch(
                r"[A-Za-z0-9](?:[A-Za-z0-9.-]{0,251}[A-Za-z0-9])?", value
            ):
                raise DeploymentConfigurationError(f"invalid {name}")
        if self.settings.caddy_container is not None and not re.fullmatch(
            r"[A-Za-z0-9][A-Za-z0-9_.-]{0,127}",
            self.settings.caddy_container,
        ):
            raise DeploymentConfigurationError("invalid JSB1_CADDY_CONTAINER")
        validate_tls_paths(
            self.settings.tls_cert_path,
            self.settings.tls_key_path,
            base_domain=domain,
            main_hostname=main_hostname,
            repository_root=PROJECT_ROOT,
        )
