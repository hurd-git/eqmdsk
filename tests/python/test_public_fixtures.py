import os
from pathlib import Path

import numpy as np
import pytest

import eqmdsk


PUBLIC_DATA = os.environ.get("EQMDSK_PUBLIC_FIXTURE_DIR")
pytestmark = pytest.mark.skipif(
    not PUBLIC_DATA,
    reason="set EQMDSK_PUBLIC_FIXTURE_DIR after running tests/public_data/fetch.py",
)


def _assert_semantically_equal(left, right):
    assert left.keys() == right.keys()
    for name in left.keys():
        left_value = left[name]
        right_value = right[name]
        if isinstance(left_value, np.ndarray):
            np.testing.assert_allclose(right_value, left_value, rtol=2e-9, atol=1e-12)
        elif isinstance(left_value, float):
            assert right_value == pytest.approx(left_value, rel=2e-9, abs=1e-12)
        else:
            assert right_value == left_value


@pytest.mark.parametrize(
    ("filename", "file_type"),
    [
        ("freeqdsk-test-1.aeqdsk", eqmdsk.AFile),
        ("freeqdsk-test-1.geqdsk", eqmdsk.GFile),
    ],
)
def test_public_file_parse_write_parse(tmp_path: Path, filename, file_type):
    source = Path(PUBLIC_DATA) / filename
    assert source.is_file(), f"public fixture was not fetched: {source}"

    original = file_type(source)
    output = tmp_path / (filename + ".roundtrip")
    original.write(output)
    reparsed = file_type(output)

    _assert_semantically_equal(original, reparsed)
    assert [item.name for item in reparsed.raw_sections] == [
        item.name for item in original.raw_sections
    ]
    if file_type is eqmdsk.AFile:
        assert reparsed.footer == original.footer
    else:
        assert reparsed.extra_header == original.extra_header
        assert reparsed.extension_tail == original.extension_tail
