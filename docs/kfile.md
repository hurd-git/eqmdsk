# K-file guide

`KFile` is a loss-aware reader and writer for EFIT Fortran namelists. K-files
do not have one fixed field schema, so the API provides both a convenient
mapping of effective values and an ordered document model that retains syntax
the mapping cannot represent.

## Convenience mapping

```python
import numpy as np
from numpy.typing import NDArray

import eqmdsk

k = eqmdsk.KFile("k067590.03300")

kffcur: int = k["KFFCUR"]
xlim: NDArray[np.float64] = k["XLIM"]

k["KFFCUR"] = 2
xlim[0] += 0.01
k.write("k.modified")
```

Direct `k[name]`, `name in k`, `section()`, and `entry()` lookup is
case-insensitive, as required for Fortran identifiers. `k.fields` is the
generic `FieldMap`, exposes canonical uppercase names, and remains
case-sensitive.

For duplicate assignments, the mapping represents only the final effective
assignment. The mapping conversion is:

| Final assignment | Mapping value |
| --- | --- |
| one integer, real, logical, or string | `int`, `float`, `bool`, or `str` |
| multiple integers | writable int64 NumPy vector |
| multiple integer/real values | writable float64 NumPy vector |
| multiple strings | `list[str]` |
| indexed, null-containing, complex, raw, logical-vector, or incompatible mixed values | omitted from the mapping |

Omission from `k.keys()` does not mean input was discarded; inspect the
ordered model instead. A returned string list is a copy, so assign the entire
list back to update a mapped string vector.

## Ordered sections and entries

```python
section = k.section("IN1")
print(section.original_name, section.opener, section.terminator)

entry = k.entry("IN1", "BTOR")
for value in entry.values:
    print(value.kind, value.repeat, value.value)
```

`sections: list[NamelistSection]` preserves section order and repeated section
names. `section_count(name)` counts case-insensitive matches, and
`section(name, occurrence=0)` selects a zero-based occurrence.

A `NamelistSection` exposes:

- `name` (canonical uppercase), `original_name`, `opener`, and `terminator`;
- ordered `entries`, plus `count(name)` and `entry(name, occurrence=0)`;
- `raw_text: bytes`, `source_order`, and `source_offset`.

A `NamelistEntry` exposes:

- `name`, `original_name`, `designator`, and `subscript`;
- `values: list[NamelistValue]`;
- `raw_text: bytes`, `source_order`, `source_offset`, `parsed`, and `modified`.

The inspection lists are converted Python lists. Adding, removing, or
reordering their elements does not modify the K-file.

`raw_text` is the original source snapshot for inspection, not a regenerated
view. Likewise, `NamelistEntry.modified` tracks ordered-entry changes made by
`set()`; changing only the effective convenience mapping does not change that
flag, even though `write()` still serializes the mapped value.

## Namelist values and set()

`NamelistValue.kind` is one of `null`, `integer`, `real`, `logical`, `string`,
`complex`, or `raw`. `value` has the corresponding Python type (`None`, `int`,
`float`, `bool`, `str`, or `complex`), while `repeat` retains a compressed
Fortran repetition count. Type-specific `as_integer()`, `as_real()`,
`as_logical()`, `as_string()`, `as_complex()`, and `as_raw()` accessors raise
`FieldError` when used with the wrong kind.

Use the factories and `set()` when an entry cannot be represented safely by
mapping assignment:

```python
k.set(
    "IN1",
    "BTOR",
    [eqmdsk.NamelistValue.real(-2.1)],
)

k.set(
    "IN1",
    "OPTION",
    [
        eqmdsk.NamelistValue.integer(7),
        eqmdsk.NamelistValue.null(),
    ],
    occurrence=0,
    section_occurrence=0,
)
```

Both occurrence arguments are zero-based. `set()` only modifies an existing
section and entry; version 0.9 does not create either. Numeric arrays already
exposed as NumPy views must be edited through the view; using `set()` on such a
field raises `ValueError` so an existing view cannot be invalidated.

`NamelistValue.raw(text)` writes caller-supplied namelist syntax. eqmdsk retains
it as opaque text and cannot validate its producer-specific meaning.

## Preservation and resource limits

Section spelling, `&`/`$` openers, terminators, entry order, duplicate names,
designators/subscripts, comments, raw values, and text outside blocks are
retained. An entirely unmodified K-file is written byte-for-byte. Editing an
entry canonicalizes the changed value while preserving unrelated original
text and block-external binary bytes. `raw_sections` identifies external text
as `outside_0`, `outside_1`, and so on.

The convenience mapping expands at most ten million effective values and 64
MiB of projected string storage per file. Larger compressed assignments remain
available in `NamelistValue.repeat` without allocating their expansion.

## C++ example

```cpp
#include <eqmdsk/eqmdsk.hpp>

eqmdsk::KFile k("k067590.03300");
std::get<std::int64_t>(k.at("KFFCUR")) = 2;
k.set("IN1", "BTOR", {eqmdsk::NamelistValue::real(-2.1)});
k.write("k.modified");
```
