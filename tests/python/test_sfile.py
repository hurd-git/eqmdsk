import gc
import os
import sys
from pathlib import Path

import numpy as np
import pytest

import eqmdsk


def _write(path: Path, content: bytes) -> Path:
    path.write_bytes(content)
    return path


def test_sfile_without_titles(tmp_path):
    source = _write(
        tmp_path / "s.no-title", b"1 2 0.1 0.2\n3 4 0.3 0.4\n"
    )
    sfile = eqmdsk.SFile(source)

    assert "XLABEL" not in sfile
    assert "YLABEL" not in sfile
    assert "TITLE" not in sfile
    np.testing.assert_array_equal(sfile["X"], [1.0, 3.0])
    np.testing.assert_array_equal(sfile["Y"], [2.0, 4.0])
    np.testing.assert_array_equal(sfile["DX"], [0.1, 0.3])
    np.testing.assert_array_equal(sfile["DY"], [0.2, 0.4])


def test_sfile_empty_and_titles_only(tmp_path):
    empty = _write(tmp_path / "s.empty", b"")
    sfile = eqmdsk.SFile(empty)
    assert sfile["X"].shape == (0,)
    sfile.write()
    assert empty.read_bytes() == b""

    titled = _write(tmp_path / "s.titled", b"x\ry\rtitle\r")
    sfile = eqmdsk.SFile(titled)
    assert sfile["XLABEL"] == "x"
    assert sfile["YLABEL"] == "y"
    assert sfile["TITLE"] == "title"
    assert sfile["X"].shape == (0,)
    output = tmp_path / "s.titled.output"
    sfile.write(output)
    assert eqmdsk.SFile(output)["TITLE"] == "title"


def test_sfile_three_titles_and_fortran_exponents(tmp_path):
    source = _write(
        tmp_path / "s.three-title",
        b"major radius\r\npressure\r\nFIT TITLE\r\n"
        b"1.0D+00 2.0d+00 3.0E-1 4.0e-1\r\n",
    )
    sfile = eqmdsk.SFile(source)

    assert sfile["XLABEL"] == "major radius"
    assert sfile["YLABEL"] == "pressure"
    assert sfile["TITLE"] == "FIT TITLE"
    np.testing.assert_array_equal(sfile["X"], [1.0])
    np.testing.assert_array_equal(sfile["DY"], [0.4])


def test_sfile_normalizes_interstitial_text_on_write(tmp_path):
    source = _write(
        tmp_path / "s.extra",
        b"x label\ny label\nfit title\n"
        b"1 10 0.1 1\n"
        b"COMMENT BETWEEN\r\n"
        b"2024-run metadata\n"
        b"2 20 0.2 2\n"
        b"COMMENT AFTER\0OPAQUE",
    )
    target = tmp_path / "s.extra.roundtrip"

    sfile = eqmdsk.SFile(source)
    sfile.write(target)

    output = target.read_bytes()
    assert b"COMMENT BETWEEN" not in output
    assert b"2024-run metadata" not in output
    assert b"COMMENT AFTER" not in output
    reparsed = eqmdsk.SFile(target)
    np.testing.assert_array_equal(reparsed["X"], sfile["X"])


@pytest.mark.parametrize(
    "row",
    [
        b"1 2 3\n",
        b"1 2 3 4 5\n",
        b"1 2 broken 4\n",
    ],
)
def test_sfile_rejects_bad_data_columns(tmp_path, row):
    source = _write(tmp_path / "s.bad-columns", row)
    with pytest.raises(eqmdsk.ParseError) as caught:
        eqmdsk.SFile(source)
    assert caught.value.filename == str(source)
    assert caught.value.line == 1
    assert caught.value.column == 1


@pytest.mark.parametrize("value", ["1e999", "1e-999"])
def test_sfile_rejects_unrepresentable_data_values(tmp_path, value):
    source = _write(
        tmp_path / "s.unrepresentable",
        f"{value} 2 3 4\n".encode("ascii"),
    )
    with pytest.raises(eqmdsk.ParseError, match="finite and representable"):
        eqmdsk.SFile(source)


def test_sfile_rejects_strict_numeric_mixed_row_after_data(tmp_path):
    source = _write(
        tmp_path / "s.bad-row-after-data",
        b"0 0 0 0\n1 damaged metadata\n",
    )
    with pytest.raises(eqmdsk.ParseError) as caught:
        eqmdsk.SFile(source)
    assert caught.value.line == 2
    assert caught.value.column == 1


def test_sfile_reports_unicode_diagnostic_paths(tmp_path):
    source = _write(tmp_path / "s-坏文件-平衡", b"1 2 broken 4\n")
    with pytest.raises(eqmdsk.ParseError) as caught:
        eqmdsk.SFile(source)
    assert caught.value.filename == str(source)
    assert str(source) in str(caught.value)

    missing = tmp_path / "s-不存在-平衡"
    with pytest.raises(eqmdsk.IOError) as caught:
        eqmdsk.SFile(missing)
    assert str(missing) in str(caught.value)


@pytest.mark.skipif(
    os.name == "nt" or sys.platform == "darwin",
    reason="invalid UTF-8 filesystem paths are unavailable on this platform",
)
def test_invalid_utf8_path_does_not_break_exception_translation(tmp_path):
    source = tmp_path / os.fsdecode(b"s-invalid-\xff")
    source.write_bytes(b"1 2 broken 4\n")

    with pytest.raises(eqmdsk.ParseError) as caught:
        eqmdsk.SFile(source)
    assert "\ufffd" in caught.value.filename
    assert "\ufffd" in str(caught.value)

    missing = tmp_path / os.fsdecode(b"s-missing-\xff")
    with pytest.raises(eqmdsk.IOError) as caught:
        eqmdsk.SFile(missing)
    assert "\ufffd" in str(caught.value)


def test_sfile_max_digits_precision_roundtrip(tmp_path):
    source = _write(tmp_path / "s.precision", b"0 0 0 0\n")
    target = tmp_path / "s.precision.output"
    sfile = eqmdsk.SFile(source)
    values = np.array(
        [
            np.nextafter(1.0, 2.0),
            -np.finfo(np.float64).tiny,
            np.finfo(np.float64).max,
            np.nextafter(0.0, 1.0),
        ],
        dtype=np.float64,
    )
    sfile["X"][0], sfile["Y"][0], sfile["DX"][0], sfile["DY"][0] = values
    sfile.write(target)

    reparsed = eqmdsk.SFile(target)
    actual = np.array(
        [reparsed["X"][0], reparsed["Y"][0], reparsed["DX"][0], reparsed["DY"][0]]
    )
    np.testing.assert_array_equal(
        actual.view(np.uint64), values.view(np.uint64)
    )


def test_sfile_arrays_are_writable_and_write_defaults_to_source(tmp_path):
    source = _write(tmp_path / "s.modify", b"1 2 0.1 0.2\n")
    sfile = eqmdsk.SFile(source)

    assert sfile["Y"].flags.writeable
    view = sfile["Y"]
    assert view.dtype == np.float64
    assert view.shape == (1,)
    assert view.strides == (8,)
    assert view.flags.c_contiguous
    pointer = view.__array_interface__["data"][0]
    view[0] = 42.25
    sfile.write()
    assert eqmdsk.SFile(source)["Y"][0] == 42.25
    del sfile
    gc.collect()
    assert view.__array_interface__["data"][0] == pointer
    assert view[0] == 42.25


def test_sfile_validates_equal_lengths_and_finite_values(tmp_path):
    source = _write(tmp_path / "s.validation", b"1 2 3 4\n")
    sfile = eqmdsk.SFile(source)
    with pytest.raises(ValueError, match="preserve the existing array length"):
        sfile["DX"] = np.array([1.0, 2.0])

    sfile = eqmdsk.SFile(source)
    sfile["DY"][0] = np.inf
    with pytest.raises(eqmdsk.ValidationError):
        sfile.write(tmp_path / "nonfinite")


@pytest.mark.skipif(not Path("/dev/full").exists(), reason="requires /dev/full")
def test_sfile_reports_deferred_write_failure(tmp_path):
    source = _write(tmp_path / "s.write-error", b"1 2 3 4\n")
    sfile = eqmdsk.SFile(source)
    with pytest.raises(eqmdsk.IOError):
        sfile.write(Path("/dev/full"))
