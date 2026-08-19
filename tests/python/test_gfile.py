import gc
from pathlib import Path

import numpy as np
import pytest

import eqmdsk


WORKSPACE_DATA = Path(__file__).resolve().parents[3] / "data"
REAL_GFILE = WORKSPACE_DATA / "g067590.03300"


def _e(value):
    return f"{value:16.9E}"


def _line(values):
    return "".join(_e(value) for value in values) + "\n"


def _synthetic_gfile(path, *, tail=b"&EXTRA\n VALUE=1\n/\n\0MAG\n"):
    case = "SYNTHETIC NONSQUARE"
    content = f"{case:<48}{0:4d}{3:4d}{2:4d}\n"
    content += _line([1.0, 2.0, 3.0, 4.0, 5.0])
    content += _line([6.0, 7.0, -0.5, 0.5, -2.0])
    content += _line([4.0, -0.5, 0.0, 6.0, 0.0])
    content += _line([7.0, 0.0, 0.5, 0.0, 0.0])
    content += _line([1.0, 2.0, 3.0])  # FPOL
    content += _line([4.0, 5.0, 6.0])  # PRES
    content += _line([7.0, 8.0, 9.0])  # FFPRIM
    content += _line([10.0, 11.0, 12.0])  # PPRIME
    content += _line([100.0, 101.0, 102.0, 200.0, 201.0])
    content += _line([202.0])
    content += _line([1.0, 1.5, 2.0])  # QPSI
    content += f"{2:5d}{1:5d}\n"
    content += _line([1.0, 10.0, 2.0, 20.0])
    content += _line([3.0, 30.0])
    path.write_bytes(content.encode("ascii") + tail)


def test_synthetic_non_square_parse_write_parse(tmp_path):
    source = tmp_path / "input.with.any.suffix"
    target = tmp_path / "output.no_suffix"
    _synthetic_gfile(source)

    original = eqmdsk.GFile(source)
    assert original.filename == source
    assert original["NW"] == 3
    assert original["NH"] == 2
    np.testing.assert_array_equal(
        original["PSIRZ"], [[100.0, 101.0, 102.0], [200.0, 201.0, 202.0]]
    )
    assert original["PSIRZ"].flags.c_contiguous
    assert original.extension_tail.endswith(b"\0MAG\n")

    original["PSIRZ"][1, 2] = -99.0
    original["CURRENT"] = 8.0
    original.write(target)

    reparsed = eqmdsk.GFile(target)
    assert reparsed["PSIRZ"][1, 2] == -99.0
    assert reparsed["CURRENT"] == 8.0
    assert reparsed.extension_tail == original.extension_tail
    assert target.exists()


def test_gfile_numeric_header_and_line_ending_variants(tmp_path):
    source = tmp_path / "g.variants"
    output = tmp_path / "g.variants.out"
    _synthetic_gfile(source)
    data = source.read_bytes()
    data = data.replace(b"1.000000000E+00", b"1.000000000D+00", 1)
    data = data.replace(b" 2.000000000E+00", b" 0.200000000+001", 1)
    header, body = data.split(b"\n", 1)
    source.write_bytes(b"PREAMBLE\r\n" + header + b" SUFFIX\r\n" + body.replace(b"\n", b"\r\n"))

    gfile = eqmdsk.GFile(source)
    assert gfile["RDIM"] == 1.0
    assert gfile["ZDIM"] == 2.0
    assert gfile.extra_header == "PREAMBLE\r\n SUFFIX"
    gfile.write(output)
    reparsed = eqmdsk.GFile(output)
    assert reparsed.extra_header == gfile.extra_header
    assert reparsed.extension_tail == gfile.extension_tail


def test_gfile_whitespace_header_zero_boundary_and_bad_sizes(tmp_path):
    source = tmp_path / "g.header"
    _synthetic_gfile(source)
    data = source.read_bytes()
    _, body = data.split(b"\n", 1)
    source.write_bytes(b"WHITESPACE HEADER 0 3 2\n" + body)
    assert eqmdsk.GFile(source)["PSIRZ"].shape == (2, 3)

    _synthetic_gfile(source)
    data = source.read_bytes()
    counts = data.index(b"    2    1\n")
    tail = data.index(b"&EXTRA")
    source.write_bytes(data[:counts] + b"    0    0\n" + data[tail:])
    no_boundary = eqmdsk.GFile(source)
    assert no_boundary["NBBBS"] == 0
    assert no_boundary["LIMITR"] == 0
    assert no_boundary["RBBBS"].shape == (0,)

    _synthetic_gfile(source)
    data = source.read_bytes()
    source.write_bytes(data[:52] + b"99999999" + data[60:])
    with pytest.raises(eqmdsk.ParseError, match="dimensions"):
        eqmdsk.GFile(source)

    source.write_bytes(data[:200])
    with pytest.raises(eqmdsk.ParseError, match="unexpected end of file"):
        eqmdsk.GFile(source)


def test_gfile_rejects_unserializable_standard_header_fields(tmp_path):
    source = tmp_path / "g"
    _synthetic_gfile(source)
    gfile = eqmdsk.GFile(source)
    gfile["CASE"] = "X" * 49
    with pytest.raises(eqmdsk.ValidationError, match="48"):
        gfile.write(tmp_path / "out")

    gfile["CASE"] = "bad\ncase"
    with pytest.raises(eqmdsk.ValidationError, match="printable ASCII"):
        gfile.write(tmp_path / "out")


@pytest.mark.skipif(not REAL_GFILE.exists(), reason="local EFIT fixture unavailable")
def test_real_gfile_golden_values_and_opaque_tail(tmp_path):
    gfile = eqmdsk.GFile(REAL_GFILE)

    assert gfile["NW"] == 129
    assert gfile["NH"] == 129
    assert gfile["NBBBS"] == 94
    assert gfile["LIMITR"] == 61
    assert gfile["PSIRZ"].shape == (129, 129)
    assert gfile["PSIRZ"][0, 0] == pytest.approx(-0.509599630)
    assert gfile["PSIRZ"][64, 64] == pytest.approx(-0.535062553)
    assert gfile["PSIRZ"][128, 128] == pytest.approx(-0.0116863028)
    assert gfile["QPSI"][0] == pytest.approx(1.323673438)
    assert gfile["QPSI"][-1] == pytest.approx(11.5104681)
    assert len(gfile.extension_tail) == 383000
    assert b"&OUT1" in gfile.extension_tail
    assert gfile.extension_tail.endswith(b"\0" * 42 + b" MAG\n")
    assert gfile.cocos.candidates == [5, 6, 15, 16]

    output = tmp_path / "g-roundtrip.custom"
    gfile.write(output)
    reparsed = eqmdsk.GFile(output)
    assert reparsed.extension_tail == gfile.extension_tail
    np.testing.assert_allclose(reparsed["PSIRZ"], gfile["PSIRZ"], rtol=1e-9)


def test_cocos_selection_copy_and_inplace(tmp_path):
    source = tmp_path / "g"
    _synthetic_gfile(source, tail=b"")
    gfile = eqmdsk.GFile(source)
    assert gfile.cocos.candidates == [5, 6, 15, 16]

    with pytest.raises(eqmdsk.CocosError) as ambiguous:
        gfile.to_cocos(11)
    assert ambiguous.value.result.candidates == [5, 6, 15, 16]
    with pytest.raises(eqmdsk.CocosError) as invalid_source:
        gfile.select_cocos(1)
    assert invalid_source.value.result.candidates == [5, 6, 15, 16]

    gfile.select_cocos(5)
    converted = gfile.to_cocos(15, inplace=False)
    assert converted is not gfile
    assert gfile.cocos.selected == 5
    assert converted.cocos.selected == 15
    np.testing.assert_allclose(converted["PSIRZ"], gfile["PSIRZ"] * (2 * np.pi))
    np.testing.assert_allclose(converted["PPRIME"], gfile["PPRIME"] / (2 * np.pi))

    returned = gfile.to_cocos(15)
    assert returned is gfile
    assert gfile.cocos.selected == 15


def test_cocos_known_5_to_12_factors(tmp_path):
    source = tmp_path / "g"
    _synthetic_gfile(source, tail=b"")
    gfile = eqmdsk.GFile(source)
    gfile.select_cocos(5)
    baseline = {
        name: np.array(gfile[name], copy=True)
        for name in ("FPOL", "PPRIME", "FFPRIM", "PSIRZ", "QPSI")
    }
    baseline.update({name: gfile[name] for name in ("CURRENT", "BCENTR", "SIMAG", "SIBRY")})

    converted = gfile.to_cocos(12, inplace=False)
    two_pi = 2.0 * np.pi
    assert converted["CURRENT"] == pytest.approx(-baseline["CURRENT"])
    assert converted["BCENTR"] == pytest.approx(-baseline["BCENTR"])
    np.testing.assert_allclose(converted["FPOL"], -baseline["FPOL"])
    assert converted["SIMAG"] == pytest.approx(-baseline["SIMAG"] * two_pi)
    assert converted["SIBRY"] == pytest.approx(-baseline["SIBRY"] * two_pi)
    np.testing.assert_allclose(converted["PSIRZ"], -baseline["PSIRZ"] * two_pi)
    np.testing.assert_allclose(converted["PPRIME"], -baseline["PPRIME"] / two_pi)
    np.testing.assert_allclose(converted["FFPRIM"], -baseline["FFPRIM"] / two_pi)
    np.testing.assert_allclose(converted["QPSI"], -baseline["QPSI"])


def test_write_defaults_to_original_filename(tmp_path):
    path = tmp_path / "g.original"
    _synthetic_gfile(path)
    gfile = eqmdsk.GFile(path)
    gfile["CURRENT"] = 9.0
    gfile.write()
    assert eqmdsk.GFile(path)["CURRENT"] == 9.0


def test_gfile_array_view_keeps_owner_alive(tmp_path):
    path = tmp_path / "g"
    _synthetic_gfile(path)
    gfile = eqmdsk.GFile(path)
    view = gfile["PSIRZ"]
    pointer = view.__array_interface__["data"][0]

    del gfile
    gc.collect()

    assert view[1, 2] == 202.0
    assert view.__array_interface__["data"][0] == pointer
    view[1, 2] = 303.0
    assert view[1, 2] == 303.0


def test_dimension_change_is_rejected_when_opaque_tail_exists(tmp_path):
    path = tmp_path / "g"
    target = tmp_path / "out"
    _synthetic_gfile(path)
    gfile = eqmdsk.GFile(path)
    gfile["NW"] = 4
    with pytest.raises(eqmdsk.ValidationError, match="extension tail"):
        gfile.write(target)
    assert not target.exists()


def test_cocos_all_conventions_round_trip(tmp_path):
    conventions = {
        1: (1, 1),
        2: (1, 1),
        3: (-1, -1),
        4: (-1, -1),
        5: (1, -1),
        6: (1, -1),
        7: (-1, 1),
        8: (-1, 1),
        11: (1, 1),
        12: (1, 1),
        13: (-1, -1),
        14: (-1, -1),
        15: (1, -1),
        16: (1, -1),
        17: (-1, 1),
        18: (-1, 1),
    }
    targets = tuple(conventions)

    for source, (sigma_bp, sigma_rho) in conventions.items():
        path = tmp_path / f"g-{source}"
        _synthetic_gfile(path, tail=b"")
        base = eqmdsk.GFile(path)
        # CURRENT and QPSI are positive. Set field and flux signs to produce
        # the source convention's two observable signs.
        base["BCENTR"] = float(sigma_rho)
        base["SIMAG"] = 0.0
        base["SIBRY"] = float(sigma_bp)
        base.write(path)
        base = eqmdsk.GFile(path)
        assert source in base.cocos.candidates
        base.select_cocos(source)
        baseline = {
            name: np.array(base[name], copy=True)
            for name in ("FPOL", "PPRIME", "FFPRIM", "PSIRZ", "QPSI")
        }
        baseline.update(
            {name: base[name] for name in ("CURRENT", "BCENTR", "SIMAG", "SIBRY")}
        )

        for target in targets:
            converted = base.to_cocos(target, inplace=False)
            restored = converted.to_cocos(source, inplace=False)
            assert restored.cocos.selected == source
            for name, expected in baseline.items():
                np.testing.assert_allclose(restored[name], expected, rtol=2e-15, atol=0)


def test_cocos_unknown_and_invalid_q_are_diagnostic(tmp_path):
    path = tmp_path / "g"
    _synthetic_gfile(path, tail=b"")

    gfile = eqmdsk.GFile(path)
    gfile["QPSI"][:] = 0.0
    gfile.write(path)
    zero_q = eqmdsk.GFile(path)
    assert len(zero_q.cocos.candidates) == 8
    assert "QPSI" in zero_q.cocos.diagnostic

    zero_q["QPSI"][:] = [1.0, -1.0, 1.0]
    zero_q.write(path)
    mixed_q = eqmdsk.GFile(path)
    assert mixed_q.cocos.candidates == []
    assert "mixed signs" in mixed_q.cocos.diagnostic
