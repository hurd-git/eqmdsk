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


def test_empty_cocos_result_preserves_no_match():
    result = eqmdsk.CocosResult()
    assert result.candidates == []
    assert not result.has_match()
    assert not result.is_unique()
    with pytest.raises(eqmdsk.CocosError):
        _ = result.selected
