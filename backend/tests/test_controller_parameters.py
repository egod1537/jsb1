from __future__ import annotations

import json

import pytest
import yaml

from app.services.controller_parameters import (
    ControllerParameterError,
    RuntimeControllerParameterService,
)


def test_px4_adapter_uses_runtime_metadata_and_resolves_overrides(tmp_path) -> None:
    service = RuntimeControllerParameterService()
    catalog = service.catalog(tmp_path)
    assert catalog.source == "jsb1_px4_roll_hold_adapter"
    assert catalog.transport == "output/parameters.yaml"
    assert [item.id for item in catalog.parameters] == [
        "FW_R_TC",
        "FW_R_RMAX",
        "FW_RR_P",
        "FW_RR_I",
        "FW_RR_D",
        "FW_RR_FF",
        "FW_RR_IMAX",
    ]

    resolved = service.resolve(tmp_path, "baseline", {"FW_RR_P": 0.08})
    assert resolved.effective["FW_RR_P"] == 0.08
    assert resolved.effective["FW_RR_I"] == 0.1
    assert resolved.overrides == {"FW_RR_P": 0.08}
    assert yaml.safe_load(service.serialize(resolved.effective))["controller_parameters"] == resolved.effective


def test_parameter_contract_overrides_adapter_and_validation_is_strict(tmp_path) -> None:
    contract = tmp_path / "contract" / "execution"
    contract.mkdir(parents=True)
    (contract / "parameters.json").write_text(json.dumps({
        "transport": {"kind": "output_file", "path": "parameters.yaml"},
        "parameters": [{
            "id": "ROLL_P",
            "display_name": "Roll P",
            "default_value": 1.0,
            "minimum": 0.0,
            "maximum": 2.0,
            "variants": ["primary"],
        }],
    }), encoding="utf-8")
    service = RuntimeControllerParameterService()
    catalog = service.catalog(tmp_path)
    assert catalog.source == "jsb0_contract"
    assert catalog.transport == "output/parameters.yaml"
    assert catalog.parameters[0].id == "ROLL_P"

    with pytest.raises(ControllerParameterError, match="Unsupported controller parameters"):
        service.resolve(tmp_path, "baseline", {"ROLL_P": 1.0})
    with pytest.raises(ControllerParameterError, match="must be <= 2"):
        service.resolve(tmp_path, "primary", {"ROLL_P": 3.0})


def test_adapter_reads_defaults_from_selected_legacy_runtime(tmp_path) -> None:
    header = tmp_path / "src" / "sim" / "gnc" / "hold"
    header.mkdir(parents=True)
    (header / "Px4RollHoldReferenceController.hpp").write_text(
        """
        struct Px4RollHoldReferenceSettings {
          double timeConstantSec = 0.35;
          double maximumRollRateRadPerSec = 1.2217304763960306;
          double rateProportionalGain = 0.160;
          double rateIntegralGain = 0.080;
          double rateDerivativeGain = 0.0;
          double rateFeedForwardGain = 0.80;
          double integratorLimit = 0.15;
        };
        """,
        encoding="utf-8",
    )
    defaults = {
        item.id: item.default_value
        for item in RuntimeControllerParameterService().catalog(tmp_path).parameters
    }
    assert defaults == {
        "FW_R_TC": 0.35,
        "FW_R_RMAX": pytest.approx(70.0),
        "FW_RR_P": 0.16,
        "FW_RR_I": 0.08,
        "FW_RR_D": 0.0,
        "FW_RR_FF": 0.8,
        "FW_RR_IMAX": 0.15,
    }


def test_scenario_whitelist_limits_frozen_parameters_and_rejects_unknown_ids(
    tmp_path,
) -> None:
    service = RuntimeControllerParameterService()

    resolved = service.resolve_for_variants(
        tmp_path,
        ["baseline", "primary"],
        {"FW_RR_P": 0.08},
        ["FW_RR_P", "FW_RR_I"],
    )

    assert resolved.effective == {"FW_RR_P": 0.08, "FW_RR_I": 0.1}
    assert resolved.overrides == {"FW_RR_P": 0.08}
    assert resolved.by_variant == {
        "baseline": {"FW_RR_P": 0.08, "FW_RR_I": 0.1},
        "primary": {},
    }
    with pytest.raises(
        ControllerParameterError,
        match="Unsupported controller parameter for selected JSB0 revision: UNKNOWN_GAIN",
    ):
        service.resolve_for_variants(
            tmp_path,
            ["baseline", "primary"],
            {},
            ["FW_RR_P", "UNKNOWN_GAIN"],
        )
    with pytest.raises(
        ControllerParameterError,
        match="Unsupported controller parameters for headless execution: FW_RR_D",
    ):
        service.resolve_for_variants(
            tmp_path,
            ["baseline", "primary"],
            {"FW_RR_D": 0.01},
            ["FW_RR_P", "FW_RR_I"],
        )
