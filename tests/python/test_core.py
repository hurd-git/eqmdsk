import re
from pathlib import Path

import pytest

import eqmdsk


def test_version():
    project_root = Path(__file__).resolve().parents[2]
    cmake_version = re.search(
        r"project\(eqmdsk VERSION ([0-9]+\.[0-9]+\.[0-9]+)",
        (project_root / "CMakeLists.txt").read_text(encoding="utf-8"),
    ).group(1)
    header_version = re.search(
        r'#define EQMDSK_VERSION_STRING "([0-9]+\.[0-9]+\.[0-9]+)"',
        (project_root / "include/eqmdsk/version.hpp").read_text(encoding="utf-8"),
    ).group(1)
    pyproject_version = re.search(
        r'^version = "([0-9]+\.[0-9]+\.[0-9]+)"$',
        (project_root / "pyproject.toml").read_text(encoding="utf-8"),
        re.MULTILINE,
    ).group(1)

    assert eqmdsk.__version__ == cmake_version == header_version == pyproject_version


def test_file_mapping_repr_lists_all_fields(tmp_path):
    source = tmp_path / "s"
    source.write_text("1 2 0.1 0.2\n", encoding="ascii")
    sfile = eqmdsk.SFile(source)
    assert sfile["X"][0] == 1.0
    assert "X" in sfile
    assert "X" in sfile.keys()
    assert "X" in repr(sfile)
    assert isinstance(sfile, dict)


def test_file_mappings_are_debugger_expandable(tmp_path):
    source = tmp_path / "k"
    source.write_text("&IN1\n LIMITR=2\n&END\n", encoding="ascii")

    kfile = eqmdsk.KFile(source)
    assert isinstance(kfile, dict)
    assert isinstance(kfile["IN1"], dict)


def test_dict_mutation_helpers_cannot_bypass_core(tmp_path):
    s_path = tmp_path / "s"
    s_path.write_text("1 2 0.1 0.2\n", encoding="ascii")
    sfile = eqmdsk.SFile(s_path)

    with pytest.raises(eqmdsk.FieldError):
        sfile.update({"UNKNOWN": 1})
    with pytest.raises(eqmdsk.FieldError):
        sfile.setdefault("UNKNOWN", 1)
    with pytest.raises(TypeError):
        sfile.clear()
    with pytest.raises(TypeError):
        sfile.pop("X")
    with pytest.raises(TypeError):
        del sfile["X"]

    k_path = tmp_path / "k"
    k_path.write_text("&IN1\n LIMITR=2\n&END\n", encoding="ascii")
    kfile = eqmdsk.KFile(k_path)
    with pytest.raises(TypeError):
        kfile.update({"IN2": 1})
    with pytest.raises(TypeError):
        kfile |= {"IN2": 1}
    assert "IN2" not in kfile
    assert not dict.__contains__(kfile, "IN2")


def test_empty_cocos_result_preserves_no_match():
    result = eqmdsk.CocosResult()
    assert result.candidates == []
    assert not result.has_match()
    assert not result.is_unique()
    with pytest.raises(eqmdsk.CocosError):
        _ = result.selected
