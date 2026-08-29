#!/usr/bin/env python3
"""Small, dependency-free client for JSB1 GitHub Deployment reporting."""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
import urllib.error
import urllib.request
from typing import Any


DEFAULT_API_URL = "https://api.github.com"
DEFAULT_API_VERSION = "2026-03-10"
REPOSITORY_PATTERN = re.compile(r"^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$")
COMMIT_PATTERN = re.compile(r"^[0-9a-fA-F]{40}$")
VALID_STATES = ("in_progress", "success", "failure", "inactive")


class GitHubApiError(RuntimeError):
    """A sanitized GitHub API failure suitable for deployment logs."""


def create_deployment_payload(
    commit: str,
    environment: str,
    description: str,
    production_environment: bool,
) -> dict[str, Any]:
    return {
        "ref": commit,
        "task": "deploy",
        "auto_merge": False,
        "required_contexts": [],
        "environment": environment,
        "description": description[:140],
        "transient_environment": False,
        "production_environment": production_environment,
    }


def deployment_status_payload(
    state: str,
    environment: str,
    description: str,
    environment_url: str | None = None,
) -> dict[str, Any]:
    if state not in VALID_STATES:
        raise ValueError(f"unsupported deployment state: {state}")
    payload: dict[str, Any] = {
        "state": state,
        "environment": environment,
        "description": description[:140],
        "auto_inactive": state == "success",
    }
    if environment_url:
        payload["environment_url"] = environment_url
    return payload


def _error_message(error: urllib.error.HTTPError) -> str:
    message = ""
    try:
        body = json.loads(error.read().decode("utf-8", errors="replace"))
        if isinstance(body, dict) and isinstance(body.get("message"), str):
            message = body["message"]
    except (json.JSONDecodeError, OSError):
        pass
    message = " ".join(message.split())[:200]
    suffix = f": {message}" if message else ""
    return f"GitHub API returned HTTP {error.code}{suffix}"


def api_request(
    method: str,
    path: str,
    payload: dict[str, Any],
    *,
    token: str,
    api_url: str,
    api_version: str,
    timeout: float = 20,
) -> dict[str, Any]:
    request = urllib.request.Request(
        f"{api_url.rstrip('/')}{path}",
        data=json.dumps(payload, separators=(",", ":")).encode("utf-8"),
        headers={
            "Accept": "application/vnd.github+json",
            "Authorization": f"Bearer {token}",
            "Content-Type": "application/json",
            "User-Agent": "jsb1-deployment-reporter",
            "X-GitHub-Api-Version": api_version,
        },
        method=method,
    )
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            result = json.load(response)
    except urllib.error.HTTPError as error:
        raise GitHubApiError(_error_message(error)) from error
    except (urllib.error.URLError, TimeoutError, OSError) as error:
        reason = " ".join(str(getattr(error, "reason", error)).split())[:200]
        raise GitHubApiError(f"GitHub API request failed: {reason}") from error
    except json.JSONDecodeError as error:
        raise GitHubApiError("GitHub API returned invalid JSON") from error
    if not isinstance(result, dict):
        raise GitHubApiError("GitHub API returned an unexpected response")
    return result


def _validate_common(repository: str, environment: str) -> None:
    if not REPOSITORY_PATTERN.fullmatch(repository):
        raise ValueError("repository must use owner/name format")
    if not environment or len(environment) > 255:
        raise ValueError("environment must contain 1-255 characters")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repository", required=True)
    parser.add_argument("--api-url", default=os.environ.get("JSB1_GITHUB_API_URL", DEFAULT_API_URL))
    parser.add_argument(
        "--api-version",
        default=os.environ.get("JSB1_GITHUB_API_VERSION", DEFAULT_API_VERSION),
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    create = subparsers.add_parser("create")
    create.add_argument("--commit", required=True)
    create.add_argument("--environment", required=True)
    create.add_argument("--description", required=True)
    create.add_argument("--production-environment", action="store_true")

    status = subparsers.add_parser("status")
    status.add_argument("--deployment-id", required=True, type=int)
    status.add_argument("--state", required=True, choices=VALID_STATES)
    status.add_argument("--environment", required=True)
    status.add_argument("--description", required=True)
    status.add_argument("--environment-url")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    token = os.environ.get("JSB1_GITHUB_TOKEN") or os.environ.get("GITHUB_TOKEN")
    if not token:
        print("GitHub token is not configured", file=sys.stderr)
        return 2
    try:
        _validate_common(args.repository, args.environment)
        if args.command == "create":
            if not COMMIT_PATTERN.fullmatch(args.commit):
                raise ValueError("commit must be a full 40-character SHA")
            payload = create_deployment_payload(
                args.commit,
                args.environment,
                args.description,
                args.production_environment,
            )
            result = api_request(
                "POST",
                f"/repos/{args.repository}/deployments",
                payload,
                token=token,
                api_url=args.api_url,
                api_version=args.api_version,
            )
            deployment_id = result.get("id")
            if not isinstance(deployment_id, int) or deployment_id <= 0:
                raise GitHubApiError("GitHub deployment response did not include a valid id")
            print(deployment_id)
            return 0

        if args.deployment_id <= 0:
            raise ValueError("deployment id must be positive")
        payload = deployment_status_payload(
            args.state,
            args.environment,
            args.description,
            args.environment_url,
        )
        api_request(
            "POST",
            f"/repos/{args.repository}/deployments/{args.deployment_id}/statuses",
            payload,
            token=token,
            api_url=args.api_url,
            api_version=args.api_version,
        )
        return 0
    except (GitHubApiError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
