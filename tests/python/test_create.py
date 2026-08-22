from pathlib import Path

import numpy as np
import pytest

import eqmdsk


def _e(value: float) -> str:
    return f"{value:16.9E}"


def _synthetic_afile(path: Path) -> None:
    """Write a small standard A-file with one value in every count array."""
    lines = [
        " 01-Jan-00 00/00/0000",
        "      1               1",
        " " + _e(1.0),
        "*   1.000" + " " * 9 + "    1" + " " * 11 + "    0 LIM   1   1 QMF     0    0",
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
    lines.append("     1    1    0    0")
    lines.append(_e(21.0) + _e(22.0) + _e(999.0) + _e(999.0))
    path.write_text("\n".join(lines) + "\n", encoding="ascii")


def _fill_gfile(gfile: eqmdsk.GFile) -> None:
    scalar_fields = (
        "RDIM",
        "ZDIM",
        "RCENTR",
        "RLEFT",
        "ZMID",
        "RMAXIS",
        "ZMAXIS",
        "SIMAG",
        "SIBRY",
        "BCENTR",
        "CURRENT",
    )
    gfile["CASE"] = "created"
    for name in scalar_fields:
        gfile[name] = 1.0
    gfile["NBBBS"] = 0
    gfile["LIMITR"] = 0
    gfile["FPOL"] = np.ones(3)
    gfile["PRES"] = np.ones(3)
    gfile["FFPRIM"] = np.ones(3)
    gfile["PPRIME"] = np.ones(3)
    gfile["PSIRZ"] = np.ones((2, 3))
    gfile["QPSI"] = np.ones(3)
    gfile["RBBBS"] = np.empty(0)
    gfile["ZBBBS"] = np.empty(0)
    gfile["RLIM"] = np.empty(0)
    gfile["ZLIM"] = np.empty(0)


def test_gfile_create_requires_fields_and_roundtrips(tmp_path):
    output = tmp_path / "created.g"
    gfile = eqmdsk.GFile.create(3, 2)

    assert "AuxNamelist" in gfile
    assert type(gfile["AuxNamelist"]) is eqmdsk.Namelist
    with pytest.raises(AttributeError):
        gfile["AuxNamelist"].save(output.with_suffix(".aux"))

    assert gfile["CASE"] is None
    assert gfile["NW"] == 3
    assert gfile["NH"] == 2
    assert "CASE" in gfile.missing_fields()
    assert "NW" not in gfile.missing_fields()
    assert "KVTOR" in gfile.missing_optional_fields()
    gfile["CASE"] = None
    assert "CASE" in gfile.missing_fields()
    with pytest.raises(eqmdsk.ValidationError, match="missing required fields"):
        gfile.save(output)

    del gfile["NW"]
    assert gfile["NW"] is None
    assert "NW" in gfile.missing_fields()
    gfile["NW"] = 3

    _fill_gfile(gfile)
    gfile["AuxNamelist"]["OUT1"] = eqmdsk.NamelistBlock(
        {"ISHOT": 67590, "ITIME": 2}
    )
    assert gfile.missing_fields() == []
    gfile.save(output)

    reparsed = eqmdsk.GFile(output)
    assert reparsed["CASE"] == "created"
    assert reparsed["PSIRZ"].shape == (2, 3)
    np.testing.assert_array_equal(reparsed["QPSI"], np.ones(3))
    assert reparsed["AuxNamelist"]["OUT1"]["ISHOT"] == 67590
    assert reparsed["AuxNamelist"]["OUT1"]["ITIME"] == 2


def test_gfile_boundary_arrays_can_change_length():
    gfile = eqmdsk.GFile.create(3, 2)
    gfile["RBBBS"] = np.array([1, 2])
    gfile["ZBBBS"] = np.array([3, 4])
    gfile["RLIM"] = np.array([5])
    gfile["ZLIM"] = np.array([6])

    assert gfile["RBBBS"].shape == (2,)
    assert gfile["ZBBBS"].shape == (2,)
    assert gfile["RLIM"].shape == (1,)
    assert gfile["ZLIM"].shape == (1,)


def test_field_scalar_types_are_strict_with_real_integer_coercion():
    gfile = eqmdsk.GFile.create(3, 2)
    gfile["RLEFT"] = 1
    assert gfile["RLEFT"] == 1.0
    assert isinstance(gfile["RLEFT"], float)

    with pytest.raises(eqmdsk.FieldError, match="expects int"):
        gfile["NW"] = 3.0

    with pytest.raises(TypeError, match="float64 or integer"):
        gfile["RBBBS"] = np.array([1.0], dtype=np.float32)


def test_gfile_grid_arrays_are_derived_from_grid_fields():
    gfile = eqmdsk.GFile.create(3, 2)
    gfile["RDIM"] = 6.0
    gfile["RLEFT"] = 1.0
    gfile["ZDIM"] = 4.0
    gfile["ZMID"] = 10.0
    gfile["IPLCOUT"] = 1

    np.testing.assert_array_equal(gfile["RGRID"], [1.0, 7.0])
    np.testing.assert_array_equal(gfile["ZGRID"], [8.0, 12.0])
    with pytest.raises(ValueError):
        gfile["RGRID"][0] = 0.0
    with pytest.raises(eqmdsk.FieldError, match="derived"):
        gfile["RGRID"] = np.array([0.0, 1.0])


def test_cocos_copy_preserves_creation_field_state():
    gfile = eqmdsk.GFile.create(3, 2)
    _fill_gfile(gfile)
    converted = gfile.to_cocos(11, from_cocos=5, inplace=False)

    assert converted is not gfile
    assert converted.missing_fields() == gfile.missing_fields()
    assert converted.missing_optional_fields() == gfile.missing_optional_fields()


def test_copy_is_deep_for_all_files_and_namelist_objects():
    block = eqmdsk.NamelistBlock({"VALUE": 1, "VECTOR": np.array([1.0, 2.0])})
    block_copy = block.copy()
    block_copy["VECTOR"][0] = 9.0
    assert block["VALUE"] == 1
    np.testing.assert_array_equal(block["VECTOR"], [1.0, 2.0])

    namelist = eqmdsk.Namelist()
    namelist["IN1"] = block
    namelist_copy = namelist.copy()
    namelist_copy["IN1"]["VALUE"] = 2
    assert namelist["IN1"]["VALUE"] == 1

    gfile = eqmdsk.GFile.create(3, 2)
    _fill_gfile(gfile)
    gfile["AuxNamelist"]["OUT1"] = block
    gfile_copy = gfile.copy()
    gfile_copy["CURRENT"] = 2.0
    gfile_copy["PSIRZ"][0, 0] = 8.0
    gfile_copy["AuxNamelist"]["OUT1"]["VALUE"] = 3
    assert gfile["CURRENT"] == 1.0
    assert gfile["PSIRZ"][0, 0] == 1.0
    assert gfile["AuxNamelist"]["OUT1"]["VALUE"] == 1

    afile = eqmdsk.AFile.create()
    afile["SHOT"] = 1
    afile.header = "header"
    afile.footer = "footer"
    afile_copy = afile.copy()
    afile_copy["SHOT"] = 2
    afile_copy.header = "changed header"
    afile_copy.footer = "changed footer"
    assert afile["SHOT"] == 1
    assert afile.header == "header"
    assert afile.footer == "footer"

    kfile = eqmdsk.KFile.create()
    kfile["IN1"] = block
    kfile_copy = kfile.copy()
    kfile_copy["IN1"]["VALUE"] = 4
    assert kfile["IN1"]["VALUE"] == 1

    sfile = eqmdsk.SFile.create(1)
    sfile["X"] = np.array([1.0])
    sfile["Y"] = np.array([2.0])
    sfile["DX"] = np.array([0.1])
    sfile["DY"] = np.array([0.2])
    sfile_copy = sfile.copy()
    sfile_copy["X"][0] = 9.0
    assert sfile["X"][0] == 1.0


def test_create_without_path_requires_explicit_save_path(tmp_path):
    sfile = eqmdsk.SFile.create(1)
    assert sfile.filename == ""
    assert sfile.path == ""
    assert sfile.abspath == ""
    sfile["X"] = np.array([1.0])
    sfile["Y"] = np.array([2.0])
    sfile["DX"] = np.array([0.1])
    sfile["DY"] = np.array([0.2])
    with pytest.raises(eqmdsk.ValidationError, match="requires a path"):
        sfile.save()

    output = tmp_path / "created.s"
    sfile.save(output)
    np.testing.assert_array_equal(eqmdsk.SFile(output)["X"], [1.0])


def test_empty_gfile_save_checks_missing_path_first():
    gfile = eqmdsk.GFile.create(1, 1)
    with pytest.raises(eqmdsk.ValidationError, match="requires a path"):
        gfile.save()


def test_afile_create_from_standard_fields_roundtrips(tmp_path):
    source_path = tmp_path / "source.a"
    output = tmp_path / "created.a"
    _synthetic_afile(source_path)
    source = eqmdsk.AFile(source_path)
    created = eqmdsk.AFile.create()

    assert created["SHOT"] is None
    assert created["PBINJ"] is None
    assert "SHOT" in created.missing_fields()
    assert "PBINJ" in created.missing_optional_fields()

    for name in created.missing_fields():
        created[name] = source[name]
    assert created.missing_fields() == []
    assert created.missing_optional_fields()
    created.save(output)

    reparsed = eqmdsk.AFile(output)
    assert reparsed["SHOT"] == source["SHOT"]
    np.testing.assert_array_equal(reparsed["RCO2V"], source["RCO2V"])
    np.testing.assert_array_equal(reparsed["ECCURT"], source["ECCURT"])


def test_afile_header_and_footer_are_editable(tmp_path):
    source_path = tmp_path / "source.a"
    output = tmp_path / "edited.a"
    _synthetic_afile(source_path)
    source_path.write_bytes(source_path.read_bytes() + b"producer footer\n")

    afile = eqmdsk.AFile(source_path)
    assert "01-Jan-00" in afile.header
    assert afile.footer == "producer footer\n"
    afile.header = afile.header.replace("01-Jan-00", "02-Feb-00")
    afile.footer = "edited footer\n"
    afile.save(output)

    reparsed = eqmdsk.AFile(output)
    assert "02-Feb-00" in reparsed.header
    assert reparsed.footer == "edited footer\n"


def test_kfile_create_allows_explicit_new_blocks_and_fields(tmp_path):
    output = tmp_path / "created.k"
    kfile = eqmdsk.KFile.create()
    block = eqmdsk.NamelistBlock()
    block["LIMITR"] = 60
    block["NAME"] = "created"
    kfile["IN1"] = block
    kfile["IN2"] = eqmdsk.NamelistBlock()
    kfile["IN2"]["VALUE"] = 3.5
    assert kfile["IN1"]["LIMITR"] == 60
    assert kfile["IN2"]["VALUE"] == 3.5
    kfile.save(output)

    reparsed = eqmdsk.KFile(output)
    assert reparsed.keys() == ["IN1", "IN2"]
    assert reparsed["IN1"]["NAME"] == "created"
    assert reparsed["IN2"]["VALUE"] == 3.5


def test_standalone_namelist_is_memory_only_and_can_be_copied_to_kfile(tmp_path):
    output = tmp_path / "standalone.nml"
    namelist = eqmdsk.Namelist()
    assert not hasattr(namelist, "filename")
    assert not hasattr(namelist, "path")
    assert not hasattr(namelist, "abspath")
    assert not hasattr(namelist, "save")
    block = eqmdsk.NamelistBlock({"VALUE": 2})
    assert block._core is not None
    namelist["TEST"] = block

    kfile = eqmdsk.KFile.create()
    kfile.update(namelist)
    kfile.save(output)

    reparsed = eqmdsk.KFile(output)
    assert reparsed["TEST"]["VALUE"] == 2


def test_kfile_block_and_field_deletion_is_explicit(tmp_path):
    source = tmp_path / "k"
    source.write_text(
        "&IN1\n LIMITR=2\n VALUE=3\n/\n&IN2\n OTHER=4\n/\n",
        encoding="ascii",
    )
    kfile = eqmdsk.KFile(source)
    del kfile["IN1"]["LIMITR"]
    assert "LIMITR" not in kfile["IN1"]
    kfile["IN1"]["LIMITR"] = 60
    del kfile["IN2"]
    assert kfile.keys() == ["IN1"]
    kfile["IN3"] = eqmdsk.NamelistBlock({"VALUE": 5})
    kfile.save(tmp_path / "k.out")
    reparsed = eqmdsk.KFile(tmp_path / "k.out")
    assert reparsed.keys() == ["IN1", "IN3"]
    assert reparsed["IN1"]["LIMITR"] == 60


def test_missing_namelist_block_error_names_the_new_model(tmp_path):
    source = tmp_path / "missing-block.k"
    source.write_text("&IN1\n LIMITR=2\n/\n", encoding="ascii")
    kfile = eqmdsk.KFile(source)

    with pytest.raises(eqmdsk.FieldError, match="unknown namelist block: IN2"):
        kfile["IN2"]["ITIME"] = 2

    kfile["IN2"] = eqmdsk.NamelistBlock()
    kfile["IN2"]["ITIME"] = 2
    assert kfile["IN2"]["ITIME"] == 2


def test_existing_kfile_scalar_types_are_strict(tmp_path):
    source = tmp_path / "typed.k"
    source.write_text("&IN1\n LIMITR=2\n VALUE=3.0\n/\n", encoding="ascii")
    kfile = eqmdsk.KFile(source)

    with pytest.raises(eqmdsk.FieldError, match="expects int"):
        kfile["IN1"]["LIMITR"] = 2.5
    kfile["IN1"]["VALUE"] = 2
    assert kfile["IN1"]["VALUE"] == 2.0


def test_field_file_add_remove_and_required_none(tmp_path):
    source = tmp_path / "s"
    source.write_text("1 2 0.1 0.2\n", encoding="ascii")
    sfile = eqmdsk.SFile(source)
    sfile["TITLE"] = "created"
    del sfile["X"]
    assert sfile["X"] is None
    assert "X" in sfile.missing_fields()
    with pytest.raises(eqmdsk.ValidationError):
        sfile.save(tmp_path / "invalid")
    sfile["X"] = np.array([1.0])
    sfile.pop("TITLE")
    assert "TITLE" not in sfile
    sfile.save(tmp_path / "s.out")


@pytest.mark.skipif(
    not (Path(__file__).resolve().parents[2] / "data/g067590.03300").exists(),
    reason="local EFIT fixture unavailable",
)
def test_gfile_aux_block_and_extension_field_mutation(tmp_path):
    source = Path(__file__).resolve().parents[2] / "data/g067590.03300"
    output = tmp_path / "g.out"
    gfile = eqmdsk.GFile(source)

    aux = gfile["AuxNamelist"]
    aux["NEW_SECTION"] = eqmdsk.NamelistBlock({"VALUE": 1})
    del aux["NEW_SECTION"]
    aux["NEW_SECTION"] = eqmdsk.NamelistBlock({"VALUE": 2})
    gfile["UNPARSED_EXTENSION"] = np.empty(0)
    del gfile["UNPARSED_EXTENSION"]
    gfile.save(output)

    reparsed = eqmdsk.GFile(output)
    assert reparsed["AuxNamelist"]["NEW_SECTION"]["VALUE"] == 2


def test_read_objects_still_reject_unknown_fields(tmp_path):
    source = tmp_path / "s"
    source.write_text("1 2 0.1 0.2\n", encoding="ascii")
    sfile = eqmdsk.SFile(source)
    with pytest.raises(eqmdsk.FieldError):
        sfile["UNKNOWN"] = 1

    created = eqmdsk.SFile.create(1)
    with pytest.raises(eqmdsk.FieldError):
        created["UNKNOWN"] = 1

    k_source = tmp_path / "k"
    k_source.write_text("&IN1\n LIMITR=2\n&END\n", encoding="ascii")
    kfile = eqmdsk.KFile(k_source)
    kfile["IN1"]["UNKNOWN"] = 1
    assert kfile["IN1"]["UNKNOWN"] == 1
    with pytest.raises(TypeError):
        kfile["IN2"] = 1
