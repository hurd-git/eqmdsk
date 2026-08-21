# G-file guide

`GFile` reads and writes the standard EFIT G/GEQDSK equilibrium exchange
format. It exposes the stored geometry, one-dimensional profiles, poloidal-flux
grid, and boundary/limiter coordinates. It does not derive coordinate arrays
or calculate magnetic-surface properties.

## Python example

```python
import numpy as np
from numpy.typing import NDArray

import eqmdsk

g = eqmdsk.GFile("g067590.03300")

nw: int = g["NW"]
nh: int = g["NH"]
current: float = g["CURRENT"]
psi: NDArray[np.float64] = g["PSIRZ"]

assert psi.shape == (nh, nw)
g["CURRENT"] = 1.2e6
psi[10, :] *= 0.99
g.write("g.modified")
```

The first `PSIRZ` index is Z and the second is R. R varies fastest in the file,
so the returned `(NH, NW)` array is C-contiguous.

## Fields

| Field | Python type and shape | Stored quantity |
| --- | --- | --- |
| `CASE` | `str` | Header title |
| `NW`, `NH` | `int` | R and Z grid sizes |
| `RDIM`, `ZDIM` | `float` | R and Z dimensions |
| `RCENTR`, `RLEFT`, `ZMID` | `float` | Reference/left/midplane coordinates |
| `RMAXIS`, `ZMAXIS` | `float` | Magnetic-axis coordinates |
| `SIMAG`, `SIBRY` | `float` | Poloidal flux at axis and boundary |
| `BCENTR`, `CURRENT` | `float` | Reference toroidal field and plasma current |
| `FPOL` | float64 `(NW,)` | Poloidal-current function profile |
| `PRES` | float64 `(NW,)` | Pressure profile |
| `FFPRIM`, `PPRIME` | float64 `(NW,)` | Stored profile derivatives |
| `QPSI` | float64 `(NW,)` | Safety-factor profile |
| `PSIRZ` | float64 `(NH, NW)` | Poloidal-flux grid, indexed `[z, r]` |
| `NBBBS`, `LIMITR` | `int` | Boundary and limiter point counts |
| `RBBBS`, `ZBBBS` | float64 `(NBBBS,)` | Boundary coordinates |
| `RLIM`, `ZLIM` | float64 `(LIMITR,)` | Limiter coordinates |

Names follow the established EFIT spelling. Units and flux sign/normalization
come from the producing code and its COCOS convention; eqmdsk does not silently
reinterpret them.

## COCOS detection and conversion

G-files do not contain enough metadata to identify every COCOS convention
uniquely. Detection therefore returns a result object rather than guessing.
eqmdsk supports the standard numbered conventions `1` through `8` and `11`
through `18`.

```python
result = g.cocos
print(result.candidates, result.diagnostic)

if result.is_unique():
    print(result.selected)
else:
    copy = g.to_cocos(to_cocos=11, from_cocos=5, inplace=False)

g.select_cocos(5)  # must be one of result.candidates
copy = g.to_cocos(11, inplace=False)  # uses g.cocos.selected
same_object = g.to_cocos(11)  # inplace=True by default
assert same_object is g
```

`to_cocos(to_cocos, from_cocos=None, inplace=True)` uses an explicit
`from_cocos` when provided. Otherwise it uses the object's unique or explicitly
selected `cocos`; only an absent or ambiguous default source raises
`CocosError`. (`from` itself cannot be a Python parameter name because it is a
reserved keyword.) An explicit source may be any supported numbered convention
and overrides detection; `select_cocos()` remains limited to detected
candidates.

The equivalent C++ methods are
`GFile::to_cocos(int target, std::optional<int> from_cocos)` for in-place
conversion and `GFile::converted_to_cocos(...)` for a converted copy.

Conversion changes only the convention-dependent stored fields:
`CURRENT`, `BCENTR`, `FPOL`, `SIMAG`, `SIBRY`, `PSIRZ`, `PPRIME`, `FFPRIM`, and
`QPSI`. Geometry, pressure, and opaque extension bytes are retained without
interpretation.

The `cocos` result is recorded at construction and updated by
`select_cocos()`/`to_cocos()`. Directly assigning convention-dependent fields
does not rerun detection; reparse a written file when a fresh detection result
is needed.

## Preserved content and accepted input

The parser accepts fixed-width and whitespace headers, CRLF, Fortran `D`
exponents, adjacent signed fields, and legacy exponent fields without an `E`.
It retains:

- input before the recognized header and a header suffix in `extra_header`;
- bytes after the standard boundary/limiter block in `extension_tail`;
- the same regions as read-only entries in `raw_sections`.

Both byte properties can contain arbitrary binary data. Their contents cannot
be edited from Python.

## Mutation and write validation

Python can replace existing scalars and strings and edit existing arrays. It
cannot resize arrays, so do not independently change `NW`, `NH`, `NBBBS`, or
`LIMITR`. In particular, changing grid dimensions while an `extension_tail` is
present is rejected because the library cannot infer the extension layout.

Before writing, eqmdsk requires:

- `CASE` to contain at most 48 printable ASCII bytes;
- `NW` and `NH` in `1..9999`, with all profile/grid shapes matching;
- boundary/limiter counts to fit their five-character records and match arrays;
- every serialized real value to be finite and representable as `E16.9`.

Violations raise `ValidationError`. Malformed or truncated input raises
`ParseError` with source location information.

## C++ example

```cpp
#include <eqmdsk/eqmdsk.hpp>

eqmdsk::GFile g("g067590.03300");
auto& psi = std::get<eqmdsk::DoubleMatrix>(g.at("PSIRZ"));
std::get<double>(g.at("CURRENT")) = 1.2e6;
psi(10, 0) *= 0.99;
g.write("g.modified");
```
