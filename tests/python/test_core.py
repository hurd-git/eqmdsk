import gc

import numpy as np
import pytest

import eqmdsk


def test_version():
    assert eqmdsk.__version__ == "0.9.0"


def test_field_names_are_exact_and_case_sensitive():
    fields = eqmdsk.FieldMap()
    fields._insert_float("CURRENT", 1.25)

    assert fields["CURRENT"] == 1.25
    assert "CURRENT" in fields
    assert "current" not in fields
    with pytest.raises(eqmdsk.FieldError):
        fields["current"]


def test_matrix_is_a_writable_c_order_view_owned_by_field_map():
    fields = eqmdsk.FieldMap()
    source = np.arange(12.0).reshape(3, 4)
    fields._insert_matrix("PSIRZ", source)

    view = fields["PSIRZ"]
    assert view.shape == (3, 4)
    assert view.dtype == np.float64
    assert view.flags.c_contiguous
    assert view.flags.writeable
    assert not np.shares_memory(view, source)

    pointer = view.__array_interface__["data"][0]
    view[1, 2] = -123.5
    assert fields["PSIRZ"][1, 2] == -123.5
    assert fields["PSIRZ"].__array_interface__["data"][0] == pointer

    del fields
    gc.collect()
    assert view[1, 2] == -123.5


def test_empty_cocos_result_preserves_no_match():
    result = eqmdsk.CocosResult()
    assert result.candidates == []
    assert not result.has_match()
    assert not result.is_unique()
    with pytest.raises(eqmdsk.CocosError):
        _ = result.selected

