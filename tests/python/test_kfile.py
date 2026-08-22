from pathlib import Path

import numpy as np
import pytest

import eqmdsk


WORKSPACE_DATA = Path(__file__).resolve().parents[2] / "data"
REAL_KFILE = WORKSPACE_DATA / "k067590.03300"


def _synthetic(path: Path) -> None:
    path.write_bytes(
        b"outside before\n"
        b"$input\n"
        b"  mixed = 1\n"
        b"  array = 2*1.5D+01, 3, .5\n"
        b"  ints = 1 2 3\n"
        b"  flag = .true.\n"
        b"  text = 'don''t', \"two words\"\n"
        b"  ignored(1:3) = 1 2 3\n"
        b"  duplicate = 1\n"
        b"  duplicate = 2\n"
        b"$end\n"
        b"&second\n value = 4\n&END\n"
    )


def test_nested_mapping_and_canonical_roundtrip(tmp_path):
    source = tmp_path / "k.input"
    output = tmp_path / "k.output"
    _synthetic(source)

    kfile = eqmdsk.KFile(source)
    assert kfile.keys() == ["INPUT", "SECOND"]
    assert kfile["input"]["MIXED"] == 1
    assert kfile["INPUT"]["duplicate"] == 2
    assert kfile["INPUT"]["FLAG"] is True
    assert kfile["INPUT"]["TEXT"] == ["don't", "two words"]
    np.testing.assert_array_equal(kfile["INPUT"]["ARRAY"], [15.0, 15.0, 3.0, 0.5])
    assert "IGNORED" not in kfile["INPUT"]
    assert "INPUT" in kfile
    assert repr(kfile).startswith("KFile({")
    assert repr(kfile["INPUT"]).startswith("{")

    kfile["INPUT"]["MIXED"] = 7
    kfile["SECOND"]["VALUE"] = 9
    kfile.save(output)
    reparsed = eqmdsk.KFile(output)
    assert reparsed["INPUT"]["MIXED"] == 7
    assert reparsed["SECOND"]["VALUE"] == 9
    assert b"outside before" not in output.read_bytes()


def test_block_and_field_mapping_helpers(tmp_path):
    source = tmp_path / "k"
    _synthetic(source)
    kfile = eqmdsk.KFile(source)
    block = kfile.get("INPUT")
    assert block is not None
    assert block.get("MIXED") == 1
    assert block.get("MISSING", 42) == 42
    assert [name for name, _ in block.items()]
    assert len(block.values()) == len(block)
    with pytest.raises(eqmdsk.FieldError):
        _ = kfile["MISSING"]


@pytest.mark.skipif(not REAL_KFILE.exists(), reason="local EFIT fixture unavailable")
def test_real_kfile_semantic_roundtrip(tmp_path):
    kfile = eqmdsk.KFile(REAL_KFILE)
    assert kfile.keys() == ["IN1"]
    assert kfile["IN1"]["KFFCUR"] == 1
    assert kfile["IN1"]["LIMITR"] == 60
    assert kfile["IN1"]["FITDELZ"] is True
    assert len(kfile["IN1"]["XLIM"]) == 60

    output = tmp_path / "real-roundtrip"
    kfile.save(output)
    reparsed = eqmdsk.KFile(output)
    assert reparsed.keys() == ["IN1"]
    assert reparsed["IN1"]["LIMITR"] == 60
    np.testing.assert_allclose(reparsed["IN1"]["XLIM"], kfile["IN1"]["XLIM"])


def test_unterminated_namelist_reports_parse_error(tmp_path):
    path = tmp_path / "bad-kfile"
    path.write_text("&IN\n VALUE=1\n", encoding="ascii")
    with pytest.raises(eqmdsk.ParseError, match="unterminated namelist"):
        eqmdsk.KFile(path)
