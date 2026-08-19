import gc
from pathlib import Path

import numpy as np
import pytest

import eqmdsk


WORKSPACE_DATA = Path(__file__).resolve().parents[3] / "data"
REAL_KFILE = WORKSPACE_DATA / "k067590.03300"


def _synthetic(path):
    path.write_bytes(
        b"outside before\n"
        b"$input\n"
        b"  MiXeD = 1\n"
        b"  array = 2*1.5D+01, 3, .5\n"
        b"  ints = 1 2 3\n"
        b"  flag = .TrUe.\n"
        b"  short_true = T\n"
        b"  short_false = FALSE\n"
        b"  text = 'don''t', \"two words\"\n"
        b"  z = (1.0D0, -2)\n"
        b"  indexed(1:3) = 1 2 3\n"
        b"  strange = foo(bar=>baz) ! retained comment\n"
        b"  nullable = 1,,3\n"
        b"  typed = 1\n"
        b"  typed = (2, 3)\n"
        b"  comma_end = 7,\n"
        b"  huge = 10000001*1\n"
        b"  ; COMMENTED_OUT = 99\n"
        b"  dup = 1\n"
        b"  dup = 2\n"
        b"$end\n"
        b"between groups\n"
        b"&second\n"
        b" value = 4\n"
        b"&END\n"
        b"&second\n"
        b" value = 5\n"
        b"/\n"
        b"outside after\0binary\n"
    )


def test_ordered_namelist_types_and_exact_unmodified_write(tmp_path):
    source = tmp_path / "k.input"
    output = tmp_path / "k.output"
    _synthetic(source)

    kfile = eqmdsk.KFile(source)
    assert [section.name for section in kfile.sections] == [
        "INPUT",
        "SECOND",
        "SECOND",
    ]
    first = kfile.section("input")
    assert first.original_name == "input"
    assert first.opener == "$"
    assert first.terminator.lower() == "$end"
    assert len(first.entries) == 17
    assert first.count("DuP") == 2
    assert first.entry("mixed").original_name == "MiXeD"
    assert first.entry("indexed").subscript == "1:3"
    assert first.entry("z").values[0].as_complex() == complex(1, -2)
    assert first.entry("strange").values[0].as_raw() == "foo(bar=>baz)"
    assert first.entry("nullable").values[1].kind == eqmdsk.NamelistValueKind.null
    assert first.entry("nullable").values[1].value is None

    assert kfile["MIXED"] == 1
    assert kfile["DUP"] == 2
    assert kfile["FLAG"] is True
    assert kfile["SHORT_TRUE"] is True
    assert kfile["SHORT_FALSE"] is False
    assert kfile["COMMA_END"] == 7
    assert "NULLABLE" not in kfile
    assert "TYPED" not in kfile
    assert "COMMENTED_OUT" not in kfile
    assert "HUGE" not in kfile
    assert first.entry("HUGE").values[0].repeat == 10_000_001
    assert kfile["TEXT"] == ["don't", "two words"]
    np.testing.assert_array_equal(kfile["ARRAY"], [15.0, 15.0, 3.0, 0.5])
    assert kfile["ARRAY"].dtype == np.float64
    np.testing.assert_array_equal(kfile["INTS"], [1, 2, 3])
    assert kfile["INTS"].dtype == np.int64

    kfile.write(output)
    assert output.read_bytes() == source.read_bytes()


def test_modify_effective_duplicate_preserves_unknown_and_outside_text(tmp_path):
    source = tmp_path / "k"
    output = tmp_path / "changed"
    _synthetic(source)
    kfile = eqmdsk.KFile(source)

    # The normal mapping represents the final effective assignment.  The
    # ordered model still retains both duplicate entries.
    kfile["MIXED"] = 7
    kfile.set("INPUT", "DUP", [eqmdsk.NamelistValue.integer(9)], 1)
    kfile["DUP"] = 2
    kfile.set(
        "SECOND",
        "VALUE",
        [eqmdsk.NamelistValue.integer(6)],
        section_occurrence=1,
    )
    kfile.write(output)

    data = output.read_bytes()
    assert b"! retained comment" in data
    assert data.endswith(b"outside after\0binary\n")
    reparsed = eqmdsk.KFile(output)
    assert reparsed["MIXED"] == 7
    assert reparsed["DUP"] == 2
    assert reparsed.entry("SECOND", "VALUE", section_occurrence=1).values[
        0
    ].as_integer() == 6
    assert reparsed.section("INPUT").count("dup") == 2
    assert reparsed.entry("INPUT", "indexed").subscript == "1:3"
    assert reparsed.entry("INPUT", "strange").values[0].as_raw() == (
        "foo(bar=>baz)"
    )


def test_array_assignment_cannot_invalidate_an_existing_view(tmp_path):
    source = tmp_path / "k"
    _synthetic(source)
    kfile = eqmdsk.KFile(source)
    view = kfile["ARRAY"]
    int_view = kfile["INTS"]
    pointer = view.__array_interface__["data"][0]
    int_pointer = int_view.__array_interface__["data"][0]
    assert view.shape == (4,)
    assert view.strides == (8,)
    assert view.flags.c_contiguous and view.flags.writeable
    assert int_view.strides == (8,)

    with pytest.raises(ValueError, match="preserve the existing array length"):
        kfile["ARRAY"] = np.arange(1000.0)
    with pytest.raises(ValueError, match="exposed array"):
        kfile.set("INPUT", "ARRAY", [eqmdsk.NamelistValue.real(1.0)])

    np.testing.assert_array_equal(view, [15.0, 15.0, 3.0, 0.5])
    view[0] = 8.0
    assert kfile["ARRAY"][0] == 8.0
    del kfile
    gc.collect()
    assert view.__array_interface__["data"][0] == pointer
    assert int_view.__array_interface__["data"][0] == int_pointer
    assert view[0] == 8.0
    assert int_view[2] == 3


def test_kfile_default_write_uses_exact_source_path(tmp_path):
    source = tmp_path / "k.no-standard-suffix"
    _synthetic(source)
    kfile = eqmdsk.KFile(source)
    kfile["MIXED"] = 41
    kfile.write()
    assert eqmdsk.KFile(source)["MIXED"] == 41


@pytest.mark.skipif(not REAL_KFILE.exists(), reason="local EFIT fixture unavailable")
def test_real_kfile_has_all_42_ordered_entries_and_roundtrips(tmp_path):
    kfile = eqmdsk.KFile(REAL_KFILE)

    assert len(kfile.sections) == 1
    assert kfile.sections[0].name == "IN1"
    assert len(kfile.sections[0].entries) == 42
    assert len(kfile.keys()) == 42
    assert kfile["KFFCUR"] == 1
    assert kfile["LIMITR"] == 60
    assert kfile["FITDELZ"] is True
    assert len(kfile["XLIM"]) == 60
    assert kfile["BTOR"] == pytest.approx(-2.2542185805)

    output = tmp_path / "real-roundtrip"
    kfile.write(output)
    reparsed = eqmdsk.KFile(output)
    assert reparsed.keys() == kfile.keys()
    np.testing.assert_allclose(reparsed["XLIM"], kfile["XLIM"])
    np.testing.assert_allclose(reparsed["COILS"], kfile["COILS"])


def test_unterminated_namelist_reports_parse_error(tmp_path):
    path = tmp_path / "bad-kfile"
    path.write_text("&IN\n VALUE=1\n", encoding="ascii")
    with pytest.raises(eqmdsk.ParseError, match="unterminated namelist"):
        eqmdsk.KFile(path)
