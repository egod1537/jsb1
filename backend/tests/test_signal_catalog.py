from pathlib import Path

import numpy as np

from app.services.runtime_contract import RuntimeContractReader
from app.services.signal_catalog import SIGNAL_CATALOG


def test_signal_catalog_covers_jsb0_roll_control_contract() -> None:
    assert set(SIGNAL_CATALOG) == {
        "commanded_roll",
        "commanded_roll_rate",
        "roll",
        "roll_rate",
        "roll_error",
        "roll_rate_error",
        "aileron",
    }
    assert SIGNAL_CATALOG["roll"].convert(np.asarray([np.pi])).tolist() == [180.0]
    assert SIGNAL_CATALOG["aileron"].convert(np.asarray([0.25])).tolist() == [0.25]
    assert SIGNAL_CATALOG["aileron"].unit == "normalized"


def test_runtime_contract_reader_loads_jsb0_yaml_signal_catalog(tmp_path: Path) -> None:
    catalog = tmp_path / "contract" / "catalog" / "signals.yaml"
    catalog.parent.mkdir(parents=True)
    catalog.write_text(
        "contract_version: 2.0.0\nsignals:\n  aircraft.roll:\n    unit: rad\n",
        encoding="utf-8",
    )

    payload = RuntimeContractReader().load_telemetry_catalog(tmp_path)

    assert payload is not None
    assert payload["signals"]["aircraft.roll"]["unit"] == "rad"


def test_runtime_contract_reader_loads_compare_only_capabilities(tmp_path: Path) -> None:
    path = tmp_path / "contract" / "execution" / "capabilities.json"
    path.parent.mkdir(parents=True)
    path.write_text(
        '{"mode":"compare","variants":["baseline","primary"]}',
        encoding="utf-8",
    )

    capability = RuntimeContractReader().load_headless_capabilities(tmp_path)

    assert capability.mode == "compare"
    assert capability.variants == ("baseline", "primary")
    assert capability.authoritative is True


def test_runtime_contract_reader_loads_current_jsb0_capabilities(tmp_path: Path) -> None:
    path = tmp_path / "contract" / "execution" / "capabilities.json"
    path.parent.mkdir(parents=True)
    path.write_text(
        '{"modes":["compare"],"variants":["baseline","primary"],'
        '"compare_variants":["baseline","primary"]}',
        encoding="utf-8",
    )

    capability = RuntimeContractReader().load_headless_capabilities(tmp_path)

    assert capability.mode == "compare"
    assert capability.variants == ("baseline", "primary")
    assert capability.authoritative is True
