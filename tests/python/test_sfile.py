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
    assert not sfile.raw_sections


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


def test_sfile_preserves_interstitial_text_at_relative_positions(tmp_path):
    source = _write(
        tmp_path / "s.extra",
        b"x label\ny label\nfit title\n"
        b"1 10 0.1 1\n"
        b"COMMENT BETWEEN\r\n"
        b"2 20 0.2 2\n"
        b"COMMENT AFTER\0OPAQUE",
    )
    target = tmp_path / "s.extra.roundtrip"

    sfile = eqmdsk.SFile(source)
    assert [section.data for section in sfile.raw_sections] == [
        b"COMMENT BETWEEN\r\n",
        b"COMMENT AFTER\0OPAQUE",
    ]
    sfile.write(target)

    output = target.read_bytes()
    assert (
        output.index(b"1 10")
        < output.index(b"COMMENT BETWEEN")
        < output.index(b"2 20")
        < output.index(b"COMMENT AFTER")
    )
    reparsed = eqmdsk.SFile(target)
    np.testing.assert_array_equal(reparsed["X"], sfile["X"])
    assert [section.data for section in reparsed.raw_sections] == [
        b"COMMENT BETWEEN\r\n",
        b"COMMENT AFTER\0OPAQUE",
    ]


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
    with pytest.raises(eqmdsk.ParseError):
        eqmdsk.SFile(source)


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
    sfile["Y"][0] = 42.25
    sfile.write()
    assert eqmdsk.SFile(source)["Y"][0] == 42.25


def test_sfile_validates_equal_lengths_and_finite_values(tmp_path):
    source = _write(tmp_path / "s.validation", b"1 2 3 4\n")
    sfile = eqmdsk.SFile(source)
    sfile["DX"] = np.array([1.0, 2.0])
    with pytest.raises(eqmdsk.ValidationError):
        sfile.write(tmp_path / "unequal")

    sfile = eqmdsk.SFile(source)
    sfile["DY"][0] = np.inf
    with pytest.raises(eqmdsk.ValidationError):
        sfile.write(tmp_path / "nonfinite")
