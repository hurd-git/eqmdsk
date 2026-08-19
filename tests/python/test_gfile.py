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
