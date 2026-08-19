# Formats and public fields

All four classes derive from `EFITFile`. `filename`, `fields`, `raw_sections`,
`keys()`, mapping access, and `write()` refer to the same C++ object from both
C++ and Python.

## GFile

The standard fields are:

| Field | Type/shape | Meaning in this library |
| --- | --- | --- |
| `CASE` | string | Header title, at most 48 printable ASCII bytes |
| `NW`, `NH` | integer | R and Z grid sizes |
| `RDIM`, `ZDIM`, `RCENTR`, `RLEFT`, `ZMID` | double | Standard GEQDSK geometry scalars |
| `RMAXIS`, `ZMAXIS`, `SIMAG`, `SIBRY` | double | Axis and flux scalars |
| `BCENTR`, `CURRENT` | double | Toroidal field and plasma current |
| `FPOL`, `PRES`, `FFPRIM`, `PPRIME`, `QPSI` | float64 `(NW,)` | Standard one-dimensional profiles |
| `PSIRZ` | float64 `(NH, NW)` | Row index is Z; column index is R; R varies fastest in the file |
| `NBBBS`, `LIMITR` | integer | Boundary and limiter point counts |
| `RBBBS`, `ZBBBS` | float64 `(NBBBS,)` | Boundary coordinates |
| `RLIM`, `ZLIM` | float64 `(LIMITR,)` | Limiter coordinates |

The parser accepts fixed and whitespace headers, CRLF, Fortran `D` exponents,
adjacent signed fields, and legacy exponents without an `E`. The writer emits
true fixed-width Fortran `E16.9`, including the no-`E` representation needed
for three-digit exponents. Data after the standard boundary block is an opaque
binary `extension_tail`; dimensions cannot change while such a tail is present.

`cocos` always returns `CocosResult`. `select_cocos(source)` accepts only a
detected candidate. Conversion currently applies only to G-files and transforms
`CURRENT`, `BCENTR`, `FPOL`, `SIMAG`, `SIBRY`, `PSIRZ`, `PPRIME`, `FFPRIM`, and
`QPSI`; geometry and pressure remain unchanged.

## AFile

Control and header fields are:

```text
SHOT TIME JFLAG LFLAG LIMLOC MCO2V MCO2R QMFLAG NLOLD NLNEW
```

For compatibility with existing OMFIT behavior, `TIME` uses the valid control
record value first, then the valid redundant third header record, and finally
`0.0` when neither copy can be parsed. A whitespace control record may omit its
`TIME` token and use the same fallback.

The six initial four-real records expose:

```text
CHISQ RCENCM BCENTR IPMEAS
IPMHD RCNTR ZCNTR AMINOR
ELONG UTRI LTRI VOLUME
RCURRT ZCURRT QSTAR BETAT
BETAP LI GAPIN GAPOUT
GAPTOP GAPBOT Q95 VERTN
```

Chord arrays are `RCO2V`, `DCO2V` with length `MCO2V`, and `RCO2R`, `DCO2R`
with length `MCO2R`. Later fixed records expose:

```text
SHEAR BPOLAV S1 S2  S3 QOUT SEPIN SEPOUT
SEPTOP SIBDRY AREA WMHD  ERROR ELONGM QM CDFLUX
ALPHA RTTT PSIREF INDENT  RSEPS[2] ZSEPS[2]
SEPEXP SEPBOT BTAXP BTAXV  AQ1 AQ2 AQ3 DSEP
RM ZM PSIM TAUMHD  BETAPD BETATD WDIA DIAMAG
VLOOP TAUDIA QMERCI TAVEM
```

Response counts `NSILOP0`, `MAGPRI0`, `NFCOIL0`, and `NESUM0` control arrays
`CSILOP`, `CMPR2`, `CCBRSP`, and `ECCURT`. Up to 15 contiguous optional
four-real records are recognized:

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

Text before the `*` control record and data after the last recognized optional
record are retained as binary header/footer regions.

## KFile

K-files use an ordered Fortran namelist model:

- `NamelistSection` preserves section order, original spelling, `&`/`$` opener,
  `/`/`&END`/`$END` terminator, and ordered entries;
- `NamelistEntry` preserves duplicate variables, designator/subscript, comments,
  source offsets, raw text, and typed values;
- `NamelistValueKind` is `null`, `integer`, `real`, `logical`, `string`,
  `complex`, or `raw`; repetition counts remain compressed;
- block-external text is retained byte-for-byte.

Names exposed in the effective `FieldMap` are uppercase. Direct `KFile`
mapping lookup and the section/entry APIs are case-insensitive; the generic
`kfile.fields` object is an ordinary `FieldMap` and requires its canonical
uppercase keys. The effective map contains the final assignment only when it
can be represented by `FieldValue`; indexed assignments, null-containing lists,
complex/raw values, and logical vectors remain available through the ordered
entry model. Its per-file expansion budgets are ten million effective values
and 64 MiB of projected string storage. Larger compressed data remain ordered
values without being expanded into the convenience map.

`set(section, name, values, occurrence=0, section_occurrence=0)` modifies an
existing entry. Version 0.9 does not create new sections or variables. In
Python, exposed numeric arrays are edited through their NumPy view; `set()` is
reserved for non-array entries so it cannot invalidate a live view.

## SFile

Up to three leading text records are `XLABEL`, `YLABEL`, and `TITLE`. Every data
record has exactly four finite real values, exposed as equal-length float64
vectors `X`, `Y`, `DX`, and `DY`. Text after data starts is anchored to the
number of preceding data rows and retained across write/reparse. Empty and
title-only files are valid.
