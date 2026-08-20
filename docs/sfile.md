# S-file guide

`SFile` reads and writes the simple EFIT S-file form: up to three leading text
records followed by rows of exactly four real values. eqmdsk names the columns
`X`, `Y`, `DX`, and `DY` but does not infer units or perform curve analysis.

## Python example

```python
import numpy as np
from numpy.typing import NDArray

import eqmdsk

s = eqmdsk.SFile("s.input")

x: NDArray[np.float64] = s["X"]
y: NDArray[np.float64] = s["Y"]
dy: NDArray[np.float64] = s["DY"]

y[:] *= 1.01
if "TITLE" in s:
    s["TITLE"] = "updated title"
s.write("s.modified")
```

## Fields

| Field | Python type/shape | Presence |
| --- | --- | --- |
| `XLABEL` | `str` | optional first leading text record |
| `YLABEL` | `str` | optional second leading text record |
| `TITLE` | `str` | optional third leading text record |
| `X`, `Y`, `DX`, `DY` | writable float64 `(N,)` | always present, including `N == 0` |

An empty file, a file with no labels, and a labels-only file are all valid.
Leading text fields are positional and contiguous. Mapping assignment can only
replace an existing field, so Python cannot add labels to a file that did not
contain them.

## Parsing and preserved text

A data record is exactly four finite, representable real tokens; Fortran `D`
exponents are accepted. A wholly numeric row with the wrong number of tokens,
or a mixed row whose first token is a valid real, raises `ParseError`. Before
the first data row, a malformed numeric-looking first token is also an error.
After data has started, a mixed text row whose first token merely has a numeric
prefix but is not itself a valid real can still be retained as text.

Non-data text beyond the first three labels is retained as
`RawSection(name="extra_text")` and anchored to the number of preceding data
rows. This includes text before the first data row, between rows, and after the
last row. It therefore stays in the same relative position when numeric rows
are rewritten. The bytes may contain original line endings, embedded NULs, or
non-UTF-8 data.

## Mutation and write validation

All four arrays are fixed-size NumPy views. Edit elements or slices in place;
whole-array assignment must preserve the existing length. The four lengths
must remain equal and all values must be finite. Each existing label must fit
on a single line. Invalid modified state raises `ValidationError`.

The writer emits numeric values with enough decimal precision for exact
float64 round-tripping. Labels and numeric rows may be normalized, while
preserved extra text retains its relative position and bytes.

## C++ example

```cpp
#include <eqmdsk/eqmdsk.hpp>

eqmdsk::SFile s("s.input");
auto& y = std::get<eqmdsk::DoubleVector>(s.at("Y"));
y *= 1.01;
s.write("s.modified");
```
