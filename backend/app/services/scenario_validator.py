from __future__ import annotations

from collections.abc import Iterable
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import yaml
from jsonschema import Draft202012Validator
from jsonschema.exceptions import ValidationError

from app.domain.scenario_validation import (
    ScenarioDocument,
    ScenarioDocumentError,
    ScenarioEvaluation,
    ScenarioRuntime,
    ScenarioSummary,
    ScenarioValidationError,
    ScenarioValidationPolicy,
    ScenarioValidationResult,
)


class ScenarioValidationUnavailable(RuntimeError):
    pass


@dataclass(frozen=True)
class RuntimeScenarioContract:
    validator: Draft202012Validator
    controller_parameter_ids: frozenset[str]

    @property
    def schema(self) -> dict[str, Any]:
        return self.validator.schema

    def iter_errors(self, content: dict[str, Any]):
        return self.validator.iter_errors(content)


ScenarioContract = Draft202012Validator | RuntimeScenarioContract


class ScenarioValidator:
    """Reusable YAML parsing and JSB0 runtime contract validation core."""

    def parse_yaml(self, yaml_text: str) -> ScenarioDocument:
        try:
            content = yaml.safe_load(yaml_text)
        except yaml.YAMLError as exc:
            raise ScenarioDocumentError(
                [
                    ScenarioValidationError(
                        path="$",
                        code="yaml_parse",
                        message=str(exc),
                    )
                ]
            ) from exc
        if not isinstance(content, dict):
            raise ScenarioDocumentError(
                [
                    ScenarioValidationError(
                        path="$",
                        code="type",
                        message="scenario root must be an object",
                    )
                ]
            )
        return self._document_from_content(content)

    def load_runtime_contract(
        self, runtime_root: Path, *, reader: Any | None = None
    ) -> RuntimeScenarioContract:
        # Compatibility facade. Runtime contract path knowledge lives only in
        # RuntimeContractReader.
        from app.services.runtime_contract import RuntimeContractReader

        contract_reader = reader or RuntimeContractReader()
        schema = contract_reader.load_scenario_schema(runtime_root)
        try:
            if contract_reader.is_indexed(runtime_root):
                parameter_ids = frozenset(
                    item.id for item in contract_reader.load_parameters(runtime_root)
                )
            else:
                from app.services.controller_parameters import (
                    RuntimeControllerParameterService,
                )

                parameter_ids = frozenset(
                    item.id
                    for item in RuntimeControllerParameterService(
                        contract_reader
                    ).catalog(runtime_root).parameters
                )
        except (OSError, RuntimeError, ValueError) as exc:
            raise ScenarioValidationUnavailable(
                "JSB0 Runtime controller parameter contract is unavailable"
            ) from exc
        return RuntimeScenarioContract(schema, parameter_ids)

    def validate_content(
        self,
        content: dict[str, Any],
        contract: ScenarioContract,
        *,
        runtime_branch: str | None = None,
        runtime_commit: str | None = None,
    ) -> ScenarioValidationResult:
        document = self._document_from_content(content)
        return self.validate_document(
            document,
            contract,
            runtime_branch=runtime_branch,
            runtime_commit=runtime_commit,
        )

    def validate_document(
        self,
        document: ScenarioDocument,
        contract: ScenarioContract,
        *,
        runtime_branch: str | None = None,
        runtime_commit: str | None = None,
    ) -> ScenarioValidationResult:
        content = document.content
        errors = [
            self.format_contract_error(error)
            for error in self._errors(content, contract)
        ]
        supported = getattr(contract, "controller_parameter_ids", None)
        if supported is not None:
            errors.extend(self._controller_parameter_errors(content, supported))
        return ScenarioValidationResult(
            valid=not errors,
            scenario=ScenarioSummary(name=document.name, autopilot=document.autopilot),
            runtime=self._runtime(runtime_branch, runtime_commit),
            schema_version=document.schema_version,
            errors=errors,
        )

    def evaluate_document(
        self,
        document: ScenarioDocument,
        contract: ScenarioContract,
        *,
        runtime_branch: str | None = None,
        runtime_commit: str | None = None,
        policy: ScenarioValidationPolicy,
    ) -> ScenarioEvaluation:
        return ScenarioEvaluation(
            document=document,
            result=self.validate_document(
                document,
                contract,
                runtime_branch=runtime_branch,
                runtime_commit=runtime_commit,
            ),
            policy=policy,
        )

    def validate_yaml(
        self,
        yaml_text: str,
        contract: ScenarioContract,
        *,
        runtime_branch: str | None = None,
        runtime_commit: str | None = None,
        policy: ScenarioValidationPolicy = ScenarioValidationPolicy.CATALOG_STABLE,
    ) -> ScenarioValidationResult:
        return self.evaluate_yaml(
            yaml_text,
            contract,
            runtime_branch=runtime_branch,
            runtime_commit=runtime_commit,
            policy=policy,
        ).result

    def evaluate_yaml(
        self,
        yaml_text: str,
        contract: ScenarioContract,
        *,
        runtime_branch: str | None = None,
        runtime_commit: str | None = None,
        policy: ScenarioValidationPolicy = ScenarioValidationPolicy.CATALOG_STABLE,
    ) -> ScenarioEvaluation:
        """Parse once and return the document and its contract validation together."""
        try:
            document = self.parse_yaml(yaml_text)
        except ScenarioDocumentError as exc:
            return ScenarioEvaluation(
                document=None,
                result=ScenarioValidationResult(
                    valid=False,
                    runtime=self._runtime(runtime_branch, runtime_commit),
                    errors=exc.errors,
                ),
                policy=policy,
            )
        return self.evaluate_document(
            document,
            contract,
            runtime_branch=runtime_branch,
            runtime_commit=runtime_commit,
            policy=policy,
        )

    def validate_file(
        self,
        path: Path,
        contract: ScenarioContract,
        *,
        runtime_branch: str | None = None,
        runtime_commit: str | None = None,
        policy: ScenarioValidationPolicy = ScenarioValidationPolicy.CATALOG_STABLE,
    ) -> ScenarioValidationResult:
        try:
            yaml_text = path.read_text(encoding="utf-8")
        except (OSError, UnicodeError) as exc:
            return ScenarioValidationResult(
                valid=False,
                runtime=self._runtime(runtime_branch, runtime_commit),
                errors=[
                    ScenarioValidationError(
                        path="$",
                        code="read_error",
                        message=str(exc),
                    )
                ],
            )
        return self.validate_yaml(
            yaml_text,
            contract,
            runtime_branch=runtime_branch,
            runtime_commit=runtime_commit,
            policy=policy,
        )

    def validate_many(
        self,
        scenarios: Iterable[tuple[str, str]],
        contract: ScenarioContract,
        *,
        runtime_branch: str | None = None,
        runtime_commit: str | None = None,
        policy: ScenarioValidationPolicy = ScenarioValidationPolicy.CATALOG_STABLE,
    ) -> list[tuple[str, ScenarioValidationResult]]:
        return [
            (
                scenario_id,
                self.validate_yaml(
                    yaml_text,
                    contract,
                    runtime_branch=runtime_branch,
                    runtime_commit=runtime_commit,
                    policy=policy,
                ),
            )
            for scenario_id, yaml_text in scenarios
        ]

    def validate_against_runtime(
        self,
        content: dict[str, Any],
        runtime_root: Path,
        *,
        runtime_branch: str | None = None,
        runtime_commit: str | None = None,
    ) -> ScenarioValidationResult:
        return self.validate_content(
            content,
            self.load_runtime_contract(runtime_root),
            runtime_branch=runtime_branch,
            runtime_commit=runtime_commit,
        )

    @staticmethod
    def format_contract_error(error: ValidationError) -> ScenarioValidationError:
        path_parts = [str(part) for part in error.absolute_path]
        if error.validator == "required":
            missing = next(
                (
                    name
                    for name in error.validator_value
                    if (
                        not isinstance(error.instance, dict)
                        or name not in error.instance
                    )
                ),
                None,
            )
            if missing is not None:
                path_parts.append(str(missing))
        return ScenarioValidationError(
            path=".".join(path_parts) or "$",
            code=str(error.validator),
            message=error.message,
        )

    @staticmethod
    def _errors(
        content: dict[str, Any], contract: ScenarioContract
    ) -> list[ValidationError]:
        return sorted(
            contract.iter_errors(content),
            key=lambda error: (
                tuple(str(part) for part in error.absolute_path),
                error.message,
            ),
        )

    @staticmethod
    def _controller_parameter_errors(
        content: dict[str, Any], supported: frozenset[str]
    ) -> list[ScenarioValidationError]:
        requested = content.get("controller_parameters")
        if not isinstance(requested, list):
            return []
        return [
            ScenarioValidationError(
                path=f"controller_parameters.{index}",
                code="unsupported_controller_parameter",
                message=(
                    "Unsupported controller parameter for selected JSB0 revision: "
                    f"{parameter_id}"
                ),
            )
            for index, parameter_id in enumerate(requested)
            if isinstance(parameter_id, str) and parameter_id not in supported
        ]

    def _document_from_content(self, content: dict[str, Any]) -> ScenarioDocument:
        name = content.get("name")
        parsed_name = name.strip() if isinstance(name, str) and name.strip() else None
        raw_scenario_type = content.get("scenario_type")
        scenario_type = (
            raw_scenario_type.strip()
            if isinstance(raw_scenario_type, str) and raw_scenario_type.strip()
            else None
        )
        raw_autopilot = content.get("autopilot")
        autopilot: str | None = None
        if isinstance(raw_autopilot, str) and raw_autopilot.strip():
            autopilot = raw_autopilot.strip()
        elif isinstance(raw_autopilot, dict):
            autopilot_type = raw_autopilot.get("type")
            if isinstance(autopilot_type, str) and autopilot_type.strip():
                autopilot = autopilot_type.strip()
        schema_version = content.get("schema_version")
        raw_controller_parameters = content.get("controller_parameters")
        controller_parameters = tuple(
            item.strip()
            for item in raw_controller_parameters
            if isinstance(item, str) and item.strip()
        ) if isinstance(raw_controller_parameters, list) else ()
        return ScenarioDocument(
            content=content,
            name=parsed_name,
            scenario_type=scenario_type,
            autopilot=autopilot,
            schema_version=schema_version if isinstance(schema_version, int) else None,
            controller_parameters=controller_parameters,
        )

    @staticmethod
    def _runtime(
        branch: str | None, commit: str | None
    ) -> ScenarioRuntime | None:
        if branch is None or commit is None:
            return None
        return ScenarioRuntime(branch=branch, commit=commit)
