from __future__ import annotations

import io
import json
import os
import sys
import unittest
import urllib.error
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path
from unittest import mock


SCRIPTS_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPTS_DIR))

import github_deployment  # noqa: E402


COMMIT = "a20391f7d40fe05fecba69543c40ba1a64598a20"
ROLLBACK_COMMIT = "8efb0664ab92f2df6155281415fbe33051366868"


class PayloadTests(unittest.TestCase):
    def test_create_payload_uses_exact_commit_and_disables_ref_mutation(self) -> None:
        payload = github_deployment.create_deployment_payload(
            COMMIT,
            "jsb1/feature-foo",
            "Deploy feature/foo",
            False,
        )
        self.assertEqual(payload["ref"], COMMIT)
        self.assertFalse(payload["auto_merge"])
        self.assertEqual(payload["required_contexts"], [])
        self.assertEqual(payload["environment"], "jsb1/feature-foo")
        self.assertFalse(payload["transient_environment"])
        self.assertFalse(payload["production_environment"])

    def test_main_payload_is_marked_as_production(self) -> None:
        payload = github_deployment.create_deployment_payload(
            COMMIT,
            "jsb1/main",
            "Deploy main",
            True,
        )
        self.assertTrue(payload["production_environment"])

    def test_status_payloads_cover_lifecycle(self) -> None:
        for state in ("in_progress", "success", "failure", "inactive"):
            with self.subTest(state=state):
                payload = github_deployment.deployment_status_payload(
                    state,
                    "jsb1/impl",
                    f"Deployment {state}",
                    "https://impl-jsb.mangagaki.net",
                )
                self.assertEqual(payload["state"], state)
                self.assertEqual(payload["environment"], "jsb1/impl")
                self.assertEqual(payload["environment_url"], "https://impl-jsb.mangagaki.net")
                self.assertEqual(payload["auto_inactive"], state == "success")

    def test_commit_status_payloads_cover_lifecycle_and_target(self) -> None:
        for state in ("pending", "success", "failure"):
            with self.subTest(state=state):
                payload = github_deployment.commit_status_payload(
                    state,
                    "jsb1/deploy/feature-foo",
                    f"Deployment {state}",
                    "https://feature-foo-jsb.mangagaki.net",
                )
                self.assertEqual(payload["state"], state)
                self.assertEqual(payload["context"], "jsb1/deploy/feature-foo")
                self.assertEqual(
                    payload["target_url"],
                    "https://feature-foo-jsb.mangagaki.net",
                )

    def test_verify_commit_status_response_requires_exact_sha_context_and_state(
        self,
    ) -> None:
        result = {
            "sha": COMMIT,
            "statuses": [
                {"context": "jsb1/deploy/impl", "state": "success"},
                {"context": "another/context", "state": "pending"},
            ],
        }
        github_deployment.verify_commit_status_response(
            result, COMMIT, "jsb1/deploy/impl", "success"
        )
        with self.assertRaisesRegex(
            github_deployment.GitHubApiError, "was not found"
        ):
            github_deployment.verify_commit_status_response(
                result, COMMIT, "jsb1/deploy/feature-foo", "success"
            )
        with self.assertRaisesRegex(
            github_deployment.GitHubApiError, "SHA did not match"
        ):
            github_deployment.verify_commit_status_response(
                result, ROLLBACK_COMMIT, "jsb1/deploy/impl", "success"
            )

    @mock.patch("github_deployment.urllib.request.urlopen")
    def test_api_request_serializes_json_and_authenticates_without_payload_token(
        self, urlopen: mock.Mock
    ) -> None:
        class Response(io.BytesIO):
            def __enter__(self) -> "Response":
                return self

            def __exit__(self, *_args: object) -> None:
                self.close()

        urlopen.return_value = Response(b'{"id":42}')
        payload = github_deployment.create_deployment_payload(
            COMMIT,
            "jsb1/impl",
            "Deploy impl",
            False,
        )
        result = github_deployment.api_request(
            "POST",
            "/repos/egod1537/jsb1/deployments",
            payload,
            token="secret-value",
            api_url="https://api.github.test",
            api_version="2026-03-10",
        )
        request = urlopen.call_args.args[0]
        self.assertEqual(result, {"id": 42})
        self.assertEqual(json.loads(request.data), payload)
        self.assertNotIn("secret-value", request.data.decode("utf-8"))
        self.assertEqual(request.get_header("Authorization"), "Bearer secret-value")


class CliTests(unittest.TestCase):
    @mock.patch.dict(os.environ, {"JSB1_GITHUB_TOKEN": "test-token"}, clear=True)
    @mock.patch.object(github_deployment, "api_request")
    def test_create_outputs_id_and_preserves_explicit_rollback_sha(
        self, request: mock.Mock
    ) -> None:
        request.return_value = {"id": 2718}
        output = io.StringIO()
        with redirect_stdout(output):
            result = github_deployment.main(
                [
                    "--repository",
                    "egod1537/jsb1",
                    "create",
                    "--commit",
                    ROLLBACK_COMMIT,
                    "--environment",
                    "jsb1/impl",
                    "--description",
                    "Rollback impl",
                ]
            )
        self.assertEqual(result, 0)
        self.assertEqual(output.getvalue().strip(), "2718")
        self.assertEqual(request.call_args.args[2]["ref"], ROLLBACK_COMMIT)

    @mock.patch.dict(os.environ, {"JSB1_GITHUB_TOKEN": "test-token"}, clear=True)
    @mock.patch.object(github_deployment, "api_request")
    def test_commit_status_uses_exact_sha_context_and_target(
        self, request: mock.Mock
    ) -> None:
        for commit in (COMMIT, ROLLBACK_COMMIT):
            with self.subTest(commit=commit):
                result = github_deployment.main(
                    [
                        "--repository",
                        "egod1537/jsb1",
                        "status-commit",
                        "--commit",
                        commit,
                        "--state",
                        "success",
                        "--context",
                        "jsb1/deploy/impl",
                        "--description",
                        "Deployment successful",
                        "--target-url",
                        "https://impl-jsb.mangagaki.net",
                    ]
                )
                self.assertEqual(result, 0)
                self.assertEqual(
                    request.call_args.args[1],
                    f"/repos/egod1537/jsb1/statuses/{commit}",
                )
                self.assertEqual(request.call_args.args[2]["state"], "success")
                self.assertEqual(
                    request.call_args.args[2]["context"], "jsb1/deploy/impl"
                )
                self.assertEqual(
                    request.call_args.args[2]["target_url"],
                    "https://impl-jsb.mangagaki.net",
                )

    @mock.patch.dict(os.environ, {"JSB1_GITHUB_TOKEN": "test-token"}, clear=True)
    @mock.patch.object(github_deployment, "api_request")
    def test_verify_status_reads_exact_commit_and_context(
        self, request: mock.Mock
    ) -> None:
        request.return_value = {
            "sha": COMMIT,
            "statuses": [
                {"context": "jsb1/deploy/impl", "state": "success"}
            ],
        }
        result = github_deployment.main(
            [
                "--repository",
                "egod1537/jsb1",
                "verify-status",
                "--commit",
                COMMIT,
                "--state",
                "success",
                "--context",
                "jsb1/deploy/impl",
            ]
        )
        self.assertEqual(result, 0)
        self.assertEqual(request.call_args.args[0], "GET")
        self.assertEqual(
            request.call_args.args[1],
            f"/repos/egod1537/jsb1/commits/{COMMIT}/status",
        )
        self.assertIsNone(request.call_args.args[2])

    @mock.patch.dict(os.environ, {}, clear=True)
    def test_missing_token_is_concise_and_fails_helper(self) -> None:
        error = io.StringIO()
        with redirect_stderr(error):
            result = github_deployment.main(
                [
                    "--repository",
                    "egod1537/jsb1",
                    "create",
                    "--commit",
                    COMMIT,
                    "--environment",
                    "jsb1/impl",
                    "--description",
                    "Deploy impl",
                ]
            )
        self.assertEqual(result, 2)
        self.assertEqual(error.getvalue().strip(), "GitHub token is not configured")

    def test_http_error_message_excludes_response_details_except_message(self) -> None:
        error = urllib.error.HTTPError(
            "https://api.github.test",
            403,
            "Forbidden",
            {},
            io.BytesIO(b'{"message":"rate limit exceeded"}'),
        )
        self.assertEqual(
            github_deployment._error_message(error),
            "GitHub API returned HTTP 403: rate limit exceeded",
        )
        error.close()


if __name__ == "__main__":
    unittest.main()
