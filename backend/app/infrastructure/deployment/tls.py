from __future__ import annotations

import os
import ssl
from pathlib import Path
from typing import Any

from app.domain.errors import DeploymentConfigurationError


def _dns_pattern_matches(pattern: str, hostname: str) -> bool:
    pattern = pattern.lower().rstrip(".")
    hostname = hostname.lower().rstrip(".")
    if pattern == hostname:
        return True
    if not pattern.startswith("*."):
        return False
    suffix = pattern[2:]
    return (
        hostname.endswith(f".{suffix}") and hostname.count(".") == suffix.count(".") + 1
    )


def validate_tls_paths(
    cert_path: Path | None,
    key_path: Path | None,
    *,
    base_domain: str,
    repository_root: Path,
    main_hostname: str | None = None,
) -> tuple[str, ...]:
    if cert_path is None or key_path is None:
        raise DeploymentConfigurationError(
            "JSB1_TLS_CERT_PATH and JSB1_TLS_KEY_PATH must both be configured"
        )
    cert = cert_path.expanduser().resolve()
    key = key_path.expanduser().resolve()
    for path, label in ((cert, "certificate"), (key, "private key")):
        if not path.is_file():
            raise DeploymentConfigurationError(f"TLS {label} path does not exist")
        if not os.access(path, os.R_OK):
            raise DeploymentConfigurationError(f"TLS {label} path is not readable")
        try:
            path.relative_to(repository_root.resolve())
        except ValueError:
            pass
        else:
            raise DeploymentConfigurationError(
                f"TLS {label} must remain outside the repository"
            )
    if key.stat().st_mode & 0o004:
        raise DeploymentConfigurationError("TLS private key must not be world-readable")
    try:
        decoded: dict[str, Any] = ssl._ssl._test_decode_cert(str(cert))  # type: ignore[attr-defined]
    except (OSError, ssl.SSLError, ValueError) as exc:
        raise DeploymentConfigurationError(
            "TLS certificate is not a readable X.509 certificate"
        ) from exc
    sans = tuple(
        str(value).lower().rstrip(".")
        for kind, value in decoded.get("subjectAltName", ())
        if kind == "DNS"
    )
    main_hostname = main_hostname or f"jsb.{base_domain}"
    preview_hostname = f"preview.{base_domain}"
    if not any(_dns_pattern_matches(item, main_hostname) for item in sans):
        raise DeploymentConfigurationError(
            f"TLS certificate does not cover {main_hostname}"
        )
    if not any(_dns_pattern_matches(item, preview_hostname) for item in sans):
        raise DeploymentConfigurationError(
            f"TLS certificate does not cover *.{base_domain}"
        )
    return sans
