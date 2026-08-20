"""Typed Python API for lightweight EFIT G/A/K/S file I/O."""

import builtins
from os import PathLike
from pathlib import Path
from typing import (
    Any,
    ClassVar,
    Dict,
    Iterator,
    List,
    Literal,
    Optional,
    Sequence,
    Union,
    overload,
)

import numpy as np
from numpy.typing import ArrayLike, NDArray

_PathInput = Union[str, bytes, PathLike[str], PathLike[bytes]]
_FloatArray = NDArray[np.float64]
_IntArray = NDArray[np.int64]
_AnyArray = NDArray[Any]
_FloatVectorInput = Union[_AnyArray, Sequence[float]]
_FloatMatrixInput = Union[_AnyArray, Sequence[Sequence[float]]]
_FieldReadValue = Union[
    bool,
    int,
    float,
    str,
    List[str],
    _IntArray,
    _FloatArray,
]
_FieldWriteValue = Union[bool, int, float, str, Sequence[str], ArrayLike]
_FieldTypeName = Literal[
    "bool",
    "int",
    "float",
    "str",
    "int_vector",
    "float_vector",
    "float_matrix",
    "str_vector",
]

_GStringField = Literal["CASE"]
_GIntField = Literal["NW", "NH", "NBBBS", "LIMITR"]
_GFloatField = Literal[
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
]
_GArrayField = Literal[
    "FPOL",
    "PRES",
    "FFPRIM",
    "PPRIME",
    "QPSI",
    "PSIRZ",
    "RBBBS",
    "ZBBBS",
    "RLIM",
    "ZLIM",
]
_GVectorField = Literal[
    "FPOL",
    "PRES",
    "FFPRIM",
    "PPRIME",
    "QPSI",
    "RBBBS",
    "ZBBBS",
    "RLIM",
    "ZLIM",
]
_GMatrixField = Literal["PSIRZ"]

_AStringField = Literal["LIMLOC", "QMFLAG"]
_AIntField = Literal[
    "SHOT",
    "JFLAG",
    "LFLAG",
    "MCO2V",
    "MCO2R",
    "NLOLD",
    "NLNEW",
    "NSILOP0",
    "MAGPRI0",
    "NFCOIL0",
    "NESUM0",
]
_AArrayField = Literal[
    "RCO2V",
    "DCO2V",
    "RCO2R",
    "DCO2R",
    "RSEPS",
    "ZSEPS",
    "CSILOP",
    "CMPR2",
    "CCBRSP",
    "ECCURT",
]
_AFloatField = Literal[
    "TIME",
    "CHISQ",
    "RCENCM",
    "BCENTR",
    "IPMEAS",
    "IPMHD",
    "RCNTR",
    "ZCNTR",
    "AMINOR",
    "ELONG",
    "UTRI",
    "LTRI",
    "VOLUME",
    "RCURRT",
    "ZCURRT",
    "QSTAR",
    "BETAT",
    "BETAP",
    "LI",
    "GAPIN",
    "GAPOUT",
    "GAPTOP",
    "GAPBOT",
    "Q95",
    "VERTN",
    "SHEAR",
    "BPOLAV",
    "S1",
    "S2",
    "S3",
    "QOUT",
    "SEPIN",
    "SEPOUT",
    "SEPTOP",
    "SIBDRY",
    "AREA",
    "WMHD",
    "ERROR",
    "ELONGM",
    "QM",
    "CDFLUX",
    "ALPHA",
    "RTTT",
    "PSIREF",
    "INDENT",
    "SEPEXP",
    "SEPBOT",
    "BTAXP",
    "BTAXV",
    "AQ1",
    "AQ2",
    "AQ3",
    "DSEP",
    "RM",
    "ZM",
    "PSIM",
    "TAUMHD",
    "BETAPD",
    "BETATD",
    "WDIA",
    "DIAMAG",
    "VLOOP",
    "TAUDIA",
    "QMERCI",
    "TAVEM",
    "PBINJ",
    "RVSIN",
    "ZVSIN",
    "RVSOUT",
    "ZVSOUT",
    "VSURF",
    "WPDOT",
    "WBDOT",
    "SLANTU",
    "SLANTL",
    "ZUPERTS",
    "CHIPRE",
    "CJOR95",
    "PP95",
    "DRSEP",
    "YYY2",
    "XNNC",
    "CPROF",
    "ORING",
    "CJOR0",
    "FEXPAN",
    "QMIN",
    "CHIMSE",
    "SSI01",
    "FEXPVS",
    "SEPNOSE",
    "SSI95",
    "RHOQMIN",
    "CJOR99",
    "CJ1AVE",
    "RMIDIN",
    "RMIDOUT",
    "PSURFA",
    "PEAK",
    "DMINUX",
    "DMINLX",
    "DOLUBAF",
    "DOLUBAFM",
    "DILUDOM",
    "DILUDOMM",
    "RATSOL",
    "RVSIU",
    "ZVSIU",
    "RVSID",
    "ZVSID",
    "RVSOU",
    "ZVSOU",
    "RVSOD",
    "ZVSOD",
    "CONDNO",
    "PSIN32",
    "PSIN21",
    "RQ32IN",
    "RQ21TOP",
    "CHILIBT",
    "LI3",
    "XBETAPR",
    "TFLUX",
    "TCHIMLS",
    "TWAGAP",
]

_SStringField = Literal["XLABEL", "YLABEL", "TITLE"]
_SArrayField = Literal["X", "Y", "DX", "DY"]


class Error(Exception):
    """Base class for errors reported by eqmdsk."""


class IOError(Error):
    """A file could not be opened, read, written, flushed, or closed."""


class ParseError(Error):
    """Input bytes do not form a supported file."""

    filename: str
    line: int
    column: int


class ValidationError(Error):
    """In-memory fields cannot be serialized as a valid file."""


class FieldError(Error):
    """A field, namelist section, or namelist entry is unavailable."""


class CocosResult:
    """Candidate COCOS conventions inferred from a G-file."""

    def __init__(self) -> None: ...

    @property
    def candidates(self) -> List[int]: ...
    @property
    def diagnostic(self) -> str: ...
    @property
    def selected(self) -> int:
        """Return the unique or explicitly selected convention, or raise CocosError."""
        ...
    def is_unique(self) -> bool: ...
    def is_ambiguous(self) -> bool: ...
    def has_match(self) -> bool: ...
    def __repr__(self) -> str: ...


class CocosError(Error):
    """COCOS detection or conversion needs an unambiguous source convention."""

    result: CocosResult


class RawSection:
    """Read-only opaque input region retained for round-tripping."""

    @property
    def name(self) -> str: ...
    @property
    def data(self) -> bytes: ...
    @property
    def source_offset(self) -> int: ...
    @property
    def modified(self) -> bool: ...


class FieldMap:
    """Case-sensitive mapping of canonical field names to typed values."""

    def __init__(self) -> None: ...
    def keys(self) -> List[str]: ...
    def contains(self, name: str, /) -> bool: ...
    def type_name(self, name: str, /) -> _FieldTypeName: ...
    def _insert_float(self, name: str, value: float, /) -> None: ...
    def _insert_matrix(self, name: str, value: ArrayLike, /) -> None: ...
    def __len__(self) -> int: ...
    def __contains__(self, name: str, /) -> bool: ...
    def __getitem__(self, name: str, /) -> _FieldReadValue: ...
    def __setitem__(self, name: str, value: _FieldWriteValue, /) -> None: ...
    def __iter__(self) -> Iterator[str]: ...


class EFITFile:
    """Common read-only metadata and field mapping for all file types."""

    @property
    def filename(self) -> Path: ...
    @property
    def fields(self) -> FieldMap: ...
    @property
    def raw_sections(self) -> List[RawSection]: ...
    def keys(self) -> List[str]: ...
    def __len__(self) -> int: ...
    def __contains__(self, name: str, /) -> bool: ...
    def __getitem__(self, name: str, /) -> _FieldReadValue: ...
    def __setitem__(self, name: str, value: _FieldWriteValue, /) -> None: ...


class GFile(EFITFile):
    """A complete EFIT G/GEQDSK file read eagerly from disk."""

    def __init__(self, filename: _PathInput) -> None: ...
    def write(self, path: Optional[_PathInput] = None) -> None:
        """Write to path, or to the original filename when path is None."""
        ...
    @property
    def cocos(self) -> CocosResult: ...
    def select_cocos(self, source: int) -> None: ...
    def to_cocos(self, target: int, inplace: bool = True) -> GFile:
        """Convert convention-dependent fields after selecting a valid source."""
        ...
    @property
    def extra_header(self) -> bytes: ...
    @property
    def extension_tail(self) -> bytes: ...

    @overload
    def __getitem__(self, name: _GStringField, /) -> str: ...
    @overload
    def __getitem__(self, name: _GIntField, /) -> int: ...
    @overload
    def __getitem__(self, name: _GFloatField, /) -> float: ...
    @overload
    def __getitem__(self, name: _GArrayField, /) -> _FloatArray: ...
    @overload
    def __getitem__(self, name: str, /) -> _FieldReadValue: ...
    @overload  # type: ignore[override]
    def __setitem__(self, name: _GStringField, value: str, /) -> None: ...
    @overload
    def __setitem__(self, name: _GIntField, value: int, /) -> None: ...
    @overload
    def __setitem__(self, name: _GFloatField, value: float, /) -> None: ...
    @overload
    def __setitem__(self, name: _GVectorField, value: _FloatVectorInput, /) -> None: ...
    @overload
    def __setitem__(self, name: _GMatrixField, value: _FloatMatrixInput, /) -> None: ...


class AFile(EFITFile):
    """A complete EFIT A-file read eagerly from disk."""

    def __init__(self, filename: _PathInput) -> None: ...
    def write(self, path: Optional[_PathInput] = None) -> None:
        """Write to path, or to the original filename when path is None."""
        ...
    @property
    def header(self) -> bytes: ...
    @property
    def footer(self) -> bytes: ...
    @property
    def optional_record_count(self) -> int: ...

    @overload
    def __getitem__(self, name: _AStringField, /) -> str: ...
    @overload
    def __getitem__(self, name: _AIntField, /) -> int: ...
    @overload
    def __getitem__(self, name: _AFloatField, /) -> float: ...
    @overload
    def __getitem__(self, name: _AArrayField, /) -> _FloatArray: ...
    @overload
    def __getitem__(self, name: str, /) -> _FieldReadValue: ...
    @overload  # type: ignore[override]
    def __setitem__(self, name: _AStringField, value: str, /) -> None: ...
    @overload
    def __setitem__(self, name: _AIntField, value: int, /) -> None: ...
    @overload
    def __setitem__(self, name: _AFloatField, value: float, /) -> None: ...
    @overload
    def __setitem__(self, name: _AArrayField, value: _FloatVectorInput, /) -> None: ...


class NamelistValueKind:
    """Parsed Fortran namelist value category."""

    null: ClassVar[NamelistValueKind]
    integer: ClassVar[NamelistValueKind]
    real: ClassVar[NamelistValueKind]
    logical: ClassVar[NamelistValueKind]
    string: ClassVar[NamelistValueKind]
    complex: ClassVar[NamelistValueKind]
    raw: ClassVar[NamelistValueKind]
    __members__: ClassVar[Dict[str, NamelistValueKind]]

    def __init__(self, value: int) -> None: ...
    @property
    def name(self) -> str: ...
    @property
    def value(self) -> int: ...
    def __int__(self) -> int: ...
    def __index__(self) -> int: ...
    def __hash__(self) -> int: ...
    def __eq__(self, other: object, /) -> bool: ...
    def __ne__(self, other: object, /) -> bool: ...
    def __repr__(self) -> str: ...
    def __str__(self) -> str: ...


class NamelistValue:
    """A typed namelist value and its unexpanded repetition count."""

    @staticmethod
    def null(repeat: int = 1) -> NamelistValue: ...
    @staticmethod
    def integer(value: int, repeat: int = 1) -> NamelistValue: ...
    @staticmethod
    def real(value: float, repeat: int = 1) -> NamelistValue: ...
    @staticmethod
    def logical(value: bool, repeat: int = 1) -> NamelistValue: ...
    @staticmethod
    def string(value: str, repeat: int = 1) -> NamelistValue: ...
    @staticmethod
    def complex(value: builtins.complex, repeat: int = 1) -> NamelistValue: ...
    @staticmethod
    def raw(value: str, repeat: int = 1) -> NamelistValue: ...
    @property
    def kind(self) -> NamelistValueKind: ...
    @property
    def repeat(self) -> int: ...
    @property
    def value(self) -> Union[None, int, float, bool, str, builtins.complex]: ...
    @property
    def original_text(self) -> str: ...
    def as_integer(self) -> int: ...
    def as_real(self) -> float: ...
    def as_logical(self) -> bool: ...
    def as_string(self) -> str: ...
    def as_complex(self) -> builtins.complex: ...
    def as_raw(self) -> str: ...


class NamelistEntry:
    """One ordered K-file assignment, including duplicates and designators."""

    @property
    def name(self) -> str: ...
    @property
    def original_name(self) -> str: ...
    @property
    def designator(self) -> str: ...
    @property
    def subscript(self) -> str: ...
    @property
    def values(self) -> List[NamelistValue]: ...
    @property
    def raw_text(self) -> bytes: ...
    @property
    def source_order(self) -> int: ...
    @property
    def source_offset(self) -> int: ...
    @property
    def parsed(self) -> bool: ...
    @property
    def modified(self) -> bool: ...


class NamelistSection:
    """One ordered Fortran namelist section in a K-file."""

    @property
    def name(self) -> str: ...
    @property
    def original_name(self) -> str: ...
    @property
    def opener(self) -> str: ...
    @property
    def terminator(self) -> str: ...
    @property
    def entries(self) -> List[NamelistEntry]: ...
    @property
    def raw_text(self) -> bytes: ...
    @property
    def source_order(self) -> int: ...
    @property
    def source_offset(self) -> int: ...
    def count(self, name: str) -> int: ...
    def entry(self, name: str, occurrence: int = 0) -> NamelistEntry: ...


class KFile(EFITFile):
    """An ordered, loss-aware Fortran namelist K-file."""

    def __init__(self, filename: _PathInput) -> None: ...
    def write(self, path: Optional[_PathInput] = None) -> None:
        """Write to path, or to the original filename when path is None."""
        ...
    @property
    def sections(self) -> List[NamelistSection]: ...
    def section_count(self, name: str) -> int:
        """Count case-insensitive section-name occurrences."""
        ...
    def section(self, name: str, occurrence: int = 0) -> NamelistSection:
        """Return a zero-based occurrence of a case-insensitive section name."""
        ...
    def entry(
        self,
        section_name: str,
        name: str,
        occurrence: int = 0,
        section_occurrence: int = 0,
    ) -> NamelistEntry:
        """Return an existing ordered entry; both occurrence indexes are zero-based."""
        ...
    def set(
        self,
        section_name: str,
        name: str,
        values: Sequence[NamelistValue],
        occurrence: int = 0,
        section_occurrence: int = 0,
    ) -> None:
        """Replace values on an existing entry without creating new syntax."""
        ...


class SFile(EFITFile):
    """A four-column EFIT S-file with optional labels and title."""

    def __init__(self, filename: _PathInput) -> None: ...
    def write(self, path: Optional[_PathInput] = None) -> None:
        """Write to path, or to the original filename when path is None."""
        ...

    @overload
    def __getitem__(self, name: _SStringField, /) -> str: ...
    @overload
    def __getitem__(self, name: _SArrayField, /) -> _FloatArray: ...
    @overload
    def __getitem__(self, name: str, /) -> _FieldReadValue: ...
    @overload  # type: ignore[override]
    def __setitem__(self, name: _SStringField, value: str, /) -> None: ...
    @overload
    def __setitem__(self, name: _SArrayField, value: _FloatVectorInput, /) -> None: ...


__version__: str

__all__ = [
    "AFile",
    "CocosError",
    "CocosResult",
    "Error",
    "EFITFile",
    "FieldError",
    "FieldMap",
    "GFile",
    "IOError",
    "ParseError",
    "RawSection",
    "KFile",
    "NamelistEntry",
    "NamelistSection",
    "NamelistValue",
    "NamelistValueKind",
    "SFile",
    "ValidationError",
    "__version__",
]
