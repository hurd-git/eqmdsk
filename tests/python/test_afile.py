import gc
from pathlib import Path

import numpy as np
import pytest

import eqmdsk

WORKSPACE_DATA = Path(__file__).resolve().parents[3] / "data"
REAL_AFILE = WORKSPACE_DATA / "a067590.03300"


def _e(value):
    return f"{value:16.9E}"


def _synthetic_afile(path, *, optional_count=0):
    lines = [
        " 01-Jan-00 00/00/0000",
        "      1               1",
        " " + _e(1.0),
        (
            "*"
            + f"{1.0:8.3f}"
            + " " * 9
            + f"{1:5d}"
            + " " * 11
            + f"{0:5d} LIM {3:3d} {1:3d} QMF {0:5d}{0:5d}"
        ),
    ]
    for record in range(6):
        lines.append("".join(_e(record + offset) for offset in range(1, 5)))
    lines.extend(
        [
            _e(10.0) + _e(11.0) + _e(12.0) + _e(999.0),
            _e(13.0) + _e(14.0) + _e(15.0) + _e(999.0),
            _e(14.0),
            _e(15.0),
        ]
    )
    for record in range(6, 17):
        lines.append("".join(_e(record + offset) for offset in range(1, 5)))
    lines.append(f" {1:5d}{1:5d}{0:5d}{0:5d}")
    lines.append(_e(21.0) + _e(22.0) + _e(999.0) + _e(999.0))
    for record in range(optional_count):
        lines.append(
            "".join(_e(100.0 + record * 4 + offset) for offset in range(4))
        )
    path.write_text("\n".join(lines) + "\n", encoding="ascii")


def test_synthetic_afile_and_three_digit_exponent_roundtrip(tmp_path):
    source = tmp_path / "a.synthetic"
    output = tmp_path / "a.output"
    _synthetic_afile(source)

    afile = eqmdsk.AFile(source)
    assert afile["SHOT"] == 1
    assert afile.optional_record_count == 0
    assert afile["RCO2V"].shape == (3,)
    np.testing.assert_array_equal(afile["RCO2V"], [10.0, 11.0, 12.0])
    np.testing.assert_array_equal(afile["CSILOP"], [21.0])
    np.testing.assert_array_equal(afile["CMPR2"], [22.0])
    assert afile["RCO2V"].dtype == np.float64
    assert afile["RCO2V"].strides == (8,)
    assert afile["RCO2V"].flags.c_contiguous
    assert afile["RCO2V"].flags.writeable
    view = afile["RCO2V"]
    pointer = view.__array_interface__["data"][0]
    afile["CHISQ"] = -1.0e100
    afile.write(output)

    assert b"-0.100000000+101" in output.read_bytes()
    assert eqmdsk.AFile(output)["CHISQ"] == pytest.approx(-1.0e100)
    del afile
    gc.collect()
    assert view.__array_interface__["data"][0] == pointer
    view[0] = -7.0
    assert view[0] == -7.0


def test_afile_control_numeric_header_and_line_variants(tmp_path):
    source = tmp_path / "a.variants"
    output = tmp_path / "a.variants.out"
    _synthetic_afile(source)
    lines = source.read_bytes().splitlines()
    lines[1] = b"     +1               1"
    lines[2] = b"CUSTOM HEADER TIME IS OPAQUE"
    lines[3] = b"* 1.000 +1 0 LIM 3 1 QMF 0 0"
    lines[4] = lines[4].replace(b"E+00", b"D+00", 1)
    source.write_bytes(b"\r\n".join(lines) + b"\r\n")

    afile = eqmdsk.AFile(source)
    assert afile["JFLAG"] == 1
    assert afile["CHISQ"] == 1.0
    afile.write(output)
    assert eqmdsk.AFile(output)["JFLAG"] == 1

    # Historical files may omit the redundant third header TIME line.
    lines = source.read_bytes().splitlines()
    del lines[2]
    source.write_bytes(b"\n".join(lines) + b"\n")
    afile = eqmdsk.AFile(source)
    afile.write(output)
    assert eqmdsk.AFile(output)["TIME"] == 1.0


def test_afile_all_optional_records_and_errors(tmp_path):
    source = tmp_path / "a.optional"
    _synthetic_afile(source, optional_count=15)
    afile = eqmdsk.AFile(source)
    assert afile.optional_record_count == 15
    assert afile["PBINJ"] == 100.0
    assert afile["TWAGAP"] == 159.0

    afile["LIMLOC"] = "A\n"
    with pytest.raises(eqmdsk.ValidationError, match="printable ASCII"):
        afile.write(tmp_path / "bad-control")

    _synthetic_afile(source)
    source.write_bytes(source.read_bytes()[:400])
    with pytest.raises(eqmdsk.ParseError):
        eqmdsk.AFile(source)

    _synthetic_afile(source)
    source.write_bytes(
        source.read_bytes().replace(
            b"     1    1    0    0", b"    -1    1    0    0", 1
        )
    )
    with pytest.raises(eqmdsk.ParseError, match="must not be negative"):
        eqmdsk.AFile(source)


@pytest.mark.skipif(not REAL_AFILE.exists(), reason="local EFIT fixture unavailable")
def test_real_afile_fields_footer_and_roundtrip(tmp_path):
    afile = eqmdsk.AFile(REAL_AFILE)

    assert afile.filename == REAL_AFILE
    assert afile["SHOT"] == 67590
    assert afile["TIME"] == pytest.approx(3300.0)
    assert afile["LIMLOC"] == "SNT"
    assert afile["QMFLAG"] == "CLC"
    assert afile["MCO2V"] == 3
    assert afile["MCO2R"] == 2
    np.testing.assert_allclose(
        afile["RCO2V"], [88.7076852, 120.900360, 96.1872733]
    )
    np.testing.assert_allclose(afile["RCO2R"], [90.6218729, 88.2784008])
    assert afile["NSILOP0"] == 35
    assert afile["MAGPRI0"] == 76
    assert afile["NFCOIL0"] == 12
    assert afile["NESUM0"] == 1
    assert afile["CSILOP"].shape == (35,)
    assert afile["CMPR2"].shape == (76,)
    assert afile["CCBRSP"].shape == (12,)
    assert afile["ECCURT"].shape == (1,)
    assert afile.optional_record_count == 14
    assert afile.footer.endswith(b" MAG\n")
    assert b"\0" in afile.footer

    output = tmp_path / "a-roundtrip.any-suffix"
    afile["CHISQ"] = 12.5
    afile["RCO2V"][1] = -3.25
    afile.write(output)

    reparsed = eqmdsk.AFile(output)
    assert reparsed["CHISQ"] == pytest.approx(12.5)
    assert reparsed["RCO2V"][1] == pytest.approx(-3.25)
    assert reparsed.optional_record_count == 14
    assert reparsed.footer == afile.footer


@pytest.mark.skipif(not REAL_AFILE.exists(), reason="local EFIT fixture unavailable")
def test_afile_default_write_and_strict_truncation(tmp_path):
    path = tmp_path / "a"
    path.write_bytes(REAL_AFILE.read_bytes())
    afile = eqmdsk.AFile(path)
    afile["SHOT"] = 67591
    afile.write()
    assert eqmdsk.AFile(path)["SHOT"] == 67591

    path.write_bytes(path.read_bytes()[:400])
    with pytest.raises(eqmdsk.ParseError):
        eqmdsk.AFile(path)
