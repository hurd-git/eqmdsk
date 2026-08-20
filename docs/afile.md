# A-file guide

`AFile` reads and writes EFIT A-files: fixed scalar equilibrium summaries,
diagnostic geometry/response arrays, and a contiguous prefix of optional
records. The class exposes stored values only; it does not calculate or verify
their physical relationships.

## Python example

```python
import numpy as np
from numpy.typing import NDArray

import eqmdsk

a = eqmdsk.AFile("a067590.03300")

shot: int = a["SHOT"]
time: float = a["TIME"]
chisq: float = a["CHISQ"]
rco2v: NDArray[np.float64] = a["RCO2V"]

print(shot, time, a.optional_record_count)
a["CHISQ"] = 12.5
rco2v[1] = -3.25
a.write("a.modified")
```

## Field groups

| Group | Fields | Python type/shape |
| --- | --- | --- |
| Identity/control | `SHOT`, `JFLAG`, `LFLAG`, `MCO2V`, `MCO2R`, `NLOLD`, `NLNEW` | `int` |
| Control text | `LIMLOC`, `QMFLAG` | `str` |
| Time | `TIME` | `float` |
| Initial summary | `CHISQ` through `VERTN` below | `float` |
| Vertical chords | `RCO2V`, `DCO2V` | float64 `(MCO2V,)` |
| Radial chords | `RCO2R`, `DCO2R` | float64 `(MCO2R,)` |
| Separatrix pairs | `RSEPS`, `ZSEPS` | float64 `(2,)` |
| Response counts | `NSILOP0`, `MAGPRI0`, `NFCOIL0`, `NESUM0` | `int` |
| Response arrays | `CSILOP`, `CMPR2`, `CCBRSP`, `ECCURT` | float64 with the corresponding count |
| Later/optional records | all names listed below | `float` |

The initial scalar records, in file order, are:

```text
CHISQ RCENCM BCENTR IPMEAS
IPMHD RCNTR ZCNTR AMINOR
ELONG UTRI LTRI VOLUME
RCURRT ZCURRT QSTAR BETAT
BETAP LI GAPIN GAPOUT
GAPTOP GAPBOT Q95 VERTN
```

Later required scalar records are:

```text
SHEAR BPOLAV S1 S2  S3 QOUT SEPIN SEPOUT
SEPTOP SIBDRY AREA WMHD  ERROR ELONGM QM CDFLUX
ALPHA RTTT PSIREF INDENT
SEPEXP SEPBOT BTAXP BTAXV  AQ1 AQ2 AQ3 DSEP
RM ZM PSIM TAUMHD  BETAPD BETATD WDIA DIAMAG
VLOOP TAUDIA QMERCI TAVEM
```

Up to 15 optional four-real records are recognized, in this exact order:

```text
PBINJ RVSIN ZVSIN RVSOUT  ZVSOUT VSURF WPDOT WBDOT
SLANTU SLANTL ZUPERTS CHIPRE  CJOR95 PP95 DRSEP YYY2
XNNC CPROF ORING CJOR0  FEXPAN QMIN CHIMSE SSI01
FEXPVS SEPNOSE SSI95 RHOQMIN  CJOR99 CJ1AVE RMIDIN RMIDOUT
PSURFA PEAK DMINUX DMINLX  DOLUBAF DOLUBAFM DILUDOM DILUDOMM
RATSOL RVSIU ZVSIU RVSID  ZVSID RVSOU ZVSOU RVSOD
ZVSOD CONDNO PSIN32 PSIN21  RQ32IN RQ21TOP CHILIBT LI3
XBETAPR TFLUX TCHIMLS TWAGAP
```

These are established EFIT names, but some meanings and units are producer- or
device-specific. eqmdsk preserves their values and names without introducing
aliases or a competing interpretation.

## TIME compatibility

Historical A-files may store `TIME` redundantly. eqmdsk uses the valid control
record value first, then the valid third header record, then `0.0` if neither
can be parsed. A whitespace control record may omit its time token and use the
same fallback. Writing serializes the effective `TIME` value.

## Optional records and mutation limits

`optional_record_count` reports how many leading optional records were parsed.
Optional records must be present contiguously from the first group, and each
record contains all four fields. Python mapping assignment cannot insert a
field, so it cannot append an optional record that was absent in the source.

Chord and response arrays are fixed-size writable views. Do not independently
change `MCO2V`, `MCO2R`, `NSILOP0`, `MAGPRI0`, `NFCOIL0`, or `NESUM0`; the
corresponding arrays cannot be resized through Python.

## Preserved content and validation

`header` and `footer` are read-only `bytes`. Text before the `*` control record,
an unparsed control suffix when present, and bytes after the final recognized
optional record are retained and also described by `raw_sections`. On write,
the recognized `SHOT` and redundant header `TIME` positions are patched to
match the effective fields; the surrounding unknown header bytes are retained.

Before writing, eqmdsk checks array/count consistency, requires `RSEPS` and
`ZSEPS` length 2, and requires all serialized reals to be finite and fit their
fixed-width representation. `LIMLOC` and `QMFLAG` are limited to three
printable ASCII bytes. Fixed integer and time fields must fit their A-file
widths (`I7`, `I5`, `I3`, and `F8.3` as applicable). Invalid modified state
raises `ValidationError`.

## C++ example

```cpp
#include <eqmdsk/eqmdsk.hpp>

eqmdsk::AFile a("a067590.03300");
std::get<double>(a.at("CHISQ")) = 12.5;
auto& chord = std::get<eqmdsk::DoubleVector>(a.at("RCO2V"));
chord(1) = -3.25;
a.write("a.modified");
```
