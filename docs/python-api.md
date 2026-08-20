# Python API

The Python package is a thin binding over the same C++ implementation used by
the C++ API. It ships a `py.typed` marker and an `__init__.pyi` stub, so editors
and type checkers can resolve the concrete return type of every documented
field.

## Opening and writing files

Choose the class explicitly; there is no format auto-detection or empty-file
constructor. Construction immediately reads and parses the complete file.

```python
from pathlib import Path

import eqmdsk

g = eqmdsk.GFile(Path("g123456.01234"))
a = eqmdsk.AFile("a123456.01234")
k = eqmdsk.KFile("k123456.01234")
s = eqmdsk.SFile("s123456.01234")
```

Constructors and `write()` accept strings and path-like objects. Binary path
forms accepted by Python's filesystem protocol are also represented in the
type stub. `filename` is the original path as a `pathlib.Path`.

```python
g.write()                    # overwrite the original path
g.write("copy.any-suffix")   # write exactly this path
assert g.filename == Path("g123456.01234")
```

`write(other_path)` does not change `filename`; a later no-argument `write()`
still writes the original path. A suffix is never added or interpreted.

## Mapping interface and value types

All file classes provide `keys()`, `len(file)`, `name in file`,
`file[name]`, `file[name] = value`, `fields`, and read-only `raw_sections`.
Names are the canonical uppercase EFIT names and are case-sensitive, except
for K-file direct namelist lookup.

```python
if "CURRENT" in g:
    current: float = g["CURRENT"]

for name in g.keys():
    print(name, g.fields.type_name(name))
```

The runtime conversions are:

| Stored field | Python value |
| --- | --- |
| logical | `bool` |
| integer | `int` |
| real | `float` |
| text | `str` |
| integer vector | writable `numpy.ndarray[Any, numpy.dtype[numpy.int64]]` |
| real vector/matrix | writable `numpy.ndarray[Any, numpy.dtype[numpy.float64]]` |
| text vector | `list[str]` |

The type stub uses `Literal` overloads for every fixed G-, A-, and S-file field.
For example, an editor infers `g["NW"]` as `int`, `g["CURRENT"]` as `float`,
and `g["PSIRZ"]` as a float64 NumPy array. K-file variable names are dynamic,
so their convenience mapping has the complete value union.

Looking up an unknown field raises `eqmdsk.FieldError`, not `KeyError`.
Assignment only replaces an existing value of its existing stored type; it
does not insert fields. Consequently Python cannot create a missing A-file
optional record, add labels to an unlabeled S-file, or create K-file sections
or variables through the mapping.

## NumPy arrays and mutation

Numeric arrays are writable, C-contiguous, zero-copy views of C++-owned
storage. Keeping a view alive also keeps its owner alive.

```python
import numpy as np
from numpy.typing import NDArray

psi: NDArray[np.float64] = g["PSIRZ"]
psi[0, 0] = -0.25

# Whole-array assignment copies into the same allocation.
g["PSIRZ"] = np.asarray(psi, dtype=np.float64)
```

Whole-array assignment may convert compatible input to `int64` or `float64`,
but its dimension and shape must exactly match the existing array. A mismatch
raises Python `ValueError`. Python does not expose resize operations, so count
or dimension fields must not be changed independently of their arrays.

A returned `list[str]` is a converted value, not a view. Editing that list does
not update the file; assign the complete list back when the field is exposed in
the convenience mapping.

The lists returned by `raw_sections`, K-file `sections`, section `entries`, and
entry `values` are inspection lists. Adding or removing list elements does not
change the parsed document. Use mapping assignment, in-place NumPy editing, or
`KFile.set()` for supported mutations.

## Preserved input

`raw_sections` reports opaque source regions retained for round-tripping. Each
`RawSection` has read-only `name: str`, `data: bytes`, `source_offset: int`, and
`modified: bool` properties. Format-specific byte properties such as
`GFile.extension_tail` and `AFile.footer` are also read-only. They may contain
embedded NUL or non-UTF-8 bytes.

The raw view is descriptive; it is not a second editable representation. See
the individual format pages for exactly which regions are retained.

## Exceptions

| Exception | Meaning |
| --- | --- |
| `eqmdsk.IOError` | Open, read, write, flush, or close failure; this is not `OSError` |
| `eqmdsk.ParseError` | The source is truncated or syntactically invalid |
| `eqmdsk.ValidationError` | Modified fields cannot be serialized validly |
| `eqmdsk.FieldError` | Unknown field/section/entry or wrong namelist value accessor |
| `eqmdsk.CocosError` | COCOS source is absent, ambiguous, or invalid for conversion |

All inherit from `eqmdsk.Error`. A translated `ParseError` includes
`filename`, `line`, and `column`; a translated `CocosError` includes the
`CocosResult` in `result`.

Serialization is completed and validated before opening the destination, but
replacement is not transactional. A device or filesystem failure during the
write can leave a partial destination.
