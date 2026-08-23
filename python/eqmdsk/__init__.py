"""Lightweight EFIT file I/O backed by C++.

The public classes are debugger-friendly ``dict`` facades.  C++ owns the
actual fields, namelist blocks, parsing state, and serialization; Python mapping
operations forward mutations to that core and mirror values for inspection.
Mutable NumPy values remain zero-copy views of C++ storage.
"""

from __future__ import annotations

import os
from typing import (
    Any,
    Iterable,
    Iterator,
    List,
    Mapping,
    Optional,
    Tuple,
    Type,
    TypeVar,
    Union,
)

from . import _core

_AFileCore = _core.AFile
_GFileCore = _core.GFile
_SFileCore = _core.SFile
__version__ = _core.__version__
Error = _core.Error
IOError = _core.IOError
ValidationError = _core.ValidationError
FieldError = _core.FieldError
ParseError = _core.ParseError
CocosResult = _core.CocosResult
CocosError = _core.CocosError


def _canonical_block_key(name: str) -> str:
    return name.upper()


def _field_value(core: Any, name: str) -> Any:
    """Convert one core value while preserving the public error contract."""
    try:
        return core[name]
    except UnicodeDecodeError as exc:
        raise Error(f"field {name!r} contains invalid UTF-8 text") from exc


_MISSING = object()
_PathInput = Union[str, os.PathLike[str]]
_FileT = TypeVar("_FileT", bound="_FileMapping")


def _install_extension_docs() -> None:
    """Install public documentation on C++ types that cannot carry Python docs."""
    original_init = CocosResult.__init__
    original_is_unique = CocosResult.is_unique
    original_is_ambiguous = CocosResult.is_ambiguous
    original_has_match = CocosResult.has_match

    def cocos_init(self: CocosResult) -> None:
        """Create an empty COCOS result.

        Returns:
            None. The new result has no candidates or selected source.

        Example:
            ``result = eqmdsk.CocosResult()``
        """
        original_init(self)

    def is_unique(self: CocosResult) -> bool:
        """Return whether detection found exactly one candidate.

        Returns:
            ``True`` only when ``len(candidates) == 1``. The selected source is
            deliberately ignored.

        Example:
            ``if g.cocos.is_unique(): print(g.cocos.candidates[0])``
        """
        return original_is_unique(self)

    def is_ambiguous(self: CocosResult) -> bool:
        """Return whether detection found multiple candidates.

        Returns:
            ``True`` only when ``len(candidates) > 1``.

        Example:
            ``if g.cocos.is_ambiguous(): g.select_cocos(5)``
        """
        return original_is_ambiguous(self)

    def has_match(self: CocosResult) -> bool:
        """Return whether detection found at least one candidate.

        Returns:
            ``True`` when ``candidates`` is non-empty.

        Example:
            ``if not g.cocos.has_match(): g.to_cocos(11, from_cocos=5)``
        """
        return original_has_match(self)

    cocos_init.__qualname__ = "CocosResult.__init__"
    is_unique.__qualname__ = "CocosResult.is_unique"
    is_ambiguous.__qualname__ = "CocosResult.is_ambiguous"
    has_match.__qualname__ = "CocosResult.has_match"
    CocosResult.__init__ = cocos_init
    CocosResult.is_unique = is_unique
    CocosResult.is_ambiguous = is_ambiguous
    CocosResult.has_match = has_match
    CocosResult.__doc__ = """COCOS detection and selection state exposed by GFile.

Candidates are recomputed from the G-file signs. selected is None when
detection is ambiguous or has no match, and can be explicitly chosen without
changing the candidate list.

Documentation:
    https://github.com/hurd-git/eqmdsk/blob/main/docs/python-api.md
"""
    CocosResult.candidates.__doc__ = "Detected candidate COCOS numbers."
    CocosResult.diagnostic.__doc__ = "Human-readable detection diagnostic."
    CocosResult.selected.__doc__ = (
        "Selected source COCOS number, or None when no source is selected."
    )
    Error.__doc__ = "Base class for all eqmdsk exceptions."
    IOError.__doc__ = "A file could not be opened, read, written, or closed."
    ValidationError.__doc__ = (
        "A value, shape, count, or required-field check failed."
    )
    FieldError.__doc__ = "An unknown field/block or unsupported value was requested."
    ParseError.__doc__ = (
        "Input syntax or truncation error with source location metadata."
    )
    CocosError.__doc__ = (
        "A COCOS source/target is missing, invalid, or unsupported."
    )


_install_extension_docs()


class _FileMapping(dict):
    """Internal mapping facade shared by public file and namelist classes.

    The mapping mirrors public keys for debugger expansion while C++ remains
    the owner of values and validation. Public subclasses inherit the mapping
    operations documented here.
    """

    _core_cls: Type[Any]
    _case_insensitive = False
    _display_name = "EFITFile"
    _read_only_fields: Tuple[str, ...] = ()

    def __init__(
        self,
        path: Any = None,
        *,
        _core_object: Any = None,
    ) -> None:
        """Initialize a facade around a C++ object.

        Args:
            path: Internal input path used by file subclasses.
            _core_object: Existing C++ object used by wrappers and copies.

        Returns:
            None. The new facade immediately mirrors the C++ keys.
        """
        dict.__init__(self)
        if _core_object is None:
            if path is None:
                raise TypeError("a file path is required")
            _core_object = self._core_cls(os.fspath(path))
        self._core = _core_object
        self._refresh()

    @classmethod
    def _from_core(cls: Type[_FileT], core_object: Any) -> _FileT:
        result = cls.__new__(cls)
        dict.__init__(result)
        result._core = core_object
        result._refresh()
        return result

    def _key(self, name: str) -> str:
        if self._case_insensitive:
            return _canonical_block_key(name)
        return name

    def _refresh(self) -> None:
        dict.clear(self)
        for name in self._core.keys():
            value = (
                None
                if self._core._is_missing_field(name)
                else self._value_for_display(name)
            )
            dict.__setitem__(self, name, value)
        for name in self._core._missing_fields():
            if name not in self:
                dict.__setitem__(self, name, None)
        for name in self._core._missing_optional_fields():
            if name not in self:
                dict.__setitem__(self, name, None)

    def _value_for_display(self, name: str) -> Any:
        value = _field_value(self._core, name)
        if name in self._read_only_fields and hasattr(value, "flags"):
            value.flags.writeable = False
        return value

    def keys(self) -> List[str]:  # type: ignore[override]
        """Return public field or block names in stable file order."""
        return list(dict.keys(self))

    def items(self) -> List[Tuple[str, Any]]:  # type: ignore[override]
        """Return a snapshot of (name, value) pairs."""
        return list(dict.items(self))

    def values(self) -> List[Any]:  # type: ignore[override]
        """Return a snapshot of public values in stable order."""
        return list(dict.values(self))

    def __iter__(self) -> Iterator[str]:
        return dict.__iter__(self)

    def __contains__(self, name: object) -> bool:
        if not isinstance(name, str):
            return False
        return dict.__contains__(self, self._key(name)) or self._core.__contains__(
            self._key(name)
        )

    def __getitem__(self, name: str) -> Any:
        """Return the named field or block.

        Args:
            name: Field name, or block name for namelists.

        Returns:
            The scalar, string, NumPy array, or nested mapping value.

        Raises:
            FieldError: If name is not accepted by the C++ schema.
        """
        key = self._key(name)
        if dict.__contains__(self, key):
            return dict.__getitem__(self, key)
        # Let the C++ implementation provide the established FieldError.
        return self._value_for_display(key)

    def get(self, name: str, default: Any = None) -> Any:
        if name not in self:
            return default
        return self[name]

    def __setitem__(self, name: str, value: Any) -> None:
        """Create or replace one field or block through the C++ schema.

        None represents a missing required field in a file facade and is not
        serialized as a namelist value.
        """
        key = self._key(name)
        if value is None and self._core._is_missing_field(key):
            dict.__setitem__(self, key, None)
            return
        if self._core.__contains__(key):
            self._core[key] = value
        else:
            self._core._assign(key, value)
        self._refresh()

    def _erase_field(self, name: str) -> None:
        key = self._key(name)
        if key not in self:
            raise KeyError(name)
        self._core._erase(key)
        self._refresh()

    def update(
        self,
        other: Any = (),
        /,
        **kwargs: Any,
    ) -> None:
        """Create or replace several fields or blocks.

        Args:
            other: A mapping or iterable of (name, value) pairs.
            kwargs: Additional assignments using Python keyword names.

        Returns:
            None. Each assignment is validated immediately by C++.

        Example:
            g.update({"RLEFT": 1.0, "ZMID": 0.0})
        """
        items: Iterable[Tuple[str, Any]]
        if hasattr(other, "keys"):
            items = ((key, other[key]) for key in other.keys())
        else:
            items = other
        for key, value in items:
            self[key] = value
        for key, value in kwargs.items():
            self[key] = value

    def setdefault(self, name: str, default: Any = None, /) -> Any:
        """Return an existing value or insert and return default."""
        if name in self:
            return self[name]
        self[name] = default
        return self[name]

    def __ior__(self, other: Any) -> "_FileMapping":
        self.update(other)
        return self

    def __delitem__(self, name: str) -> None:
        """Delete a field or block according to the file schema."""
        self._erase_field(name)

    def clear(self) -> None:
        for name in list(self.keys()):
            del self[name]

    def pop(self, name: str, *args: Any) -> Any:
        """Remove and return a value, optionally returning a default."""
        if len(args) > 1:
            raise TypeError(f"pop expected at most 2 arguments, got {len(args) + 1}")
        default = args[0] if args else _MISSING
        key = self._key(name)
        if key not in self:
            if default is _MISSING:
                raise KeyError(name)
            return default
        value = self[key]
        del self[key]
        return value

    def popitem(self) -> Tuple[str, Any]:
        """Remove and return the last public (name, value) pair."""
        if not self:
            raise KeyError("popitem(): mapping is empty")
        key = next(reversed(self.keys()))
        value = self[key]
        del self[key]
        return key, value

    def missing_fields(self) -> List[str]:
        """Return required fields whose current value is missing."""
        return self._core._missing_fields()

    def missing_optional_fields(self) -> List[str]:
        """Return optional schema fields that are currently absent."""
        return self._core._missing_optional_fields()

    def __getattr__(self, name: str) -> Any:
        # COCOS and format-specific read-only helpers remain owned by C++.
        return getattr(self._core, name)

    def __repr__(self) -> str:
        return f"{self._display_name}({dict.__repr__(self)})"

    def copy(self: _FileT) -> _FileT:
        """Return an independent C++-owned deep copy of this mapping."""
        return type(self)._from_core(self._core.copy())


class _PathFileMapping(_FileMapping):
    """Mapping facade for a file object that owns a path."""

    @property
    def filename(self) -> str:
        """Return the read-only base file name as ``str``."""
        return self._core.filename

    @property
    def path(self) -> str:
        """Return the read-only input path as ``str``."""
        return self._core.path

    @property
    def abspath(self) -> str:
        """Return the read-only normalized absolute path as ``str``."""
        return self._core.abspath

    def save(self, path: Optional[_PathInput] = None) -> None:
        """Write normalized text to path or the object's original path.

        Args:
            path: Optional output path. A newly created object must receive one;
                a loaded object defaults to its original path.

        Returns:
            None. The C++ serializer validates fields before opening the file.

        Details:
            Saving generates normalized format text. It preserves parsed
            semantics, not the source file's original spacing or comments.

        Example:
            ``g.save()`` or ``g.save("copy.g")``
        """
        if path is None:
            self._core.save()
        else:
            self._core.save(os.fspath(path))


class GFile(_PathFileMapping):
    """Read and write one EFIT GEQDSK G-file.

    Parameters:
        path: Existing file name or path. Use ``GFile.create`` for a new object.

    Returns:
        A mapping whose public fields and ``AuxNamelist`` are C++-owned.

    Details:
        Construction reads the complete file. Array fields are NumPy views;
        standard field rules and dimensions are checked by the C++ serializer.

    Required fields:
        G-files contain an equilibrium grid, scalar geometry and magnetic
        values, one-dimensional profiles, the two-dimensional poloidal-flux
        matrix, plasma-boundary and limiter coordinates, and sometimes EFIT
        numerical extensions or a trailing AuxNamelist. eqmdsk performs file
        I/O and validation only; it does not calculate flux-surface properties.

        Required header and scalar fields:
            CASE, NW, NH, RDIM, ZDIM, RCENTR, RLEFT, ZMID, RMAXIS,
            ZMAXIS, SIMAG, SIBRY, BCENTR, CURRENT.
        Required profiles and grid:
            FPOL, PRES, FFPRIM, PPRIME, PSIRZ, QPSI.
        Required boundary and limiter fields:
            NBBBS, LIMITR, RBBBS, ZBBBS, RLIM, ZLIM.

        Scalar values are Python int, float, or str. Numeric profiles and
        matrices are NumPy arrays backed by C++ storage. PSIRZ has shape
        (NH, NW).

    Optional fields:
        Standard EFIT extensions:
            KVTOR, RVTOR, NMASS, PRESSW, PWPRIM, DMION, RHOVN,
            KEECUR, EPOTEN.
        IPLCOUT extensions:
            IPLCOUT, IPLCOUT_NW, IPLCOUT_NH, IPLCOUT_ISHOT,
            IPLCOUT_ITIME, RGRID, ZGRID, IPLCOUT_PREFIX, PCURRT,
            PCURRZ, CJOR, R1SURF, R2SURF, VOLP, BPOLSS.
            PCURRT and PCURRZ are layout-specific names for the same kind of
            unnamed IPLCOUT R-Z grid record: PCURRT is used for IPLCOUT=1,
            while PCURRZ is used for IPLCOUT=2. OMFIT commonly exposes both
            layouts as PCURRT; this is an application-level naming difference,
            not a file-format conversion. Both matrices have shape (NH, NW).
        Compatibility-preserved extension:
            UNPARSED_EXTENSION stores numerical extension data whose standard
            meaning cannot be identified safely.

        Any other top-level field name is rejected with FieldError. Dynamic
        producer-specific fields belong in AuxNamelist blocks instead.

    Reading and editing:
        Fields use the normal mapping interface:

            >>> g = eqmdsk.GFile("g067590.03300")
            >>> nw = g["NW"]
            >>> psi = g["PSIRZ"]
            >>> current = g.get("CURRENT")
            >>> g["CURRENT"] = 2.0
            >>> g["PSIRZ"][0, 0] = -0.25
            >>> g.update({"RLEFT": 1.0, "ZMID": 0.0})
            >>> g.save("g.modified")

        keys(), items(), values(), get(), update(), pop(), del, membership,
        iteration, len(), and copy() follow dictionary-style behavior.
        copy() returns an independent deep copy of fields, arrays, path
        metadata, and AuxNamelist.

        Deleting a required field does not remove its key: its value becomes
        None and it appears in missing_fields(). Deleting an optional field
        removes it. missing_optional_fields() lists supported optional fields
        not currently present.

    Array and count rules:
        Whole-array assignment replaces the C++ array. Length and shape checks
        are performed by save(), allowing related fields to be edited in any
        order. RBBBS and ZBBBS must have NBBBS elements; RLIM and ZLIM must
        have LIMITR elements. PSIRZ and the active IPLCOUT matrix (PCURRT for
        mode 1 or PCURRZ for mode 2) must have shape (NH, NW).
        RGRID and ZGRID are read-only derived arrays calculated from RLEFT,
        RDIM, ZMID, ZDIM, NW, and NH.

        Integer controls such as NW, NH, NBBBS, and LIMITR reject floating-point
        values. Real scalar fields accept Python integers and convert them to
        float. Real arrays normally use numpy.float64; integer arrays may be
        converted to float64, while incompatible dtypes are rejected.

    AuxNamelist:
        g["AuxNamelist"] is always the Namelist owned by this GFile, including
        when the input has no namelist tail. Its block names are dynamic and
        are not restricted to OUT1, BASIS, or CHIOUT:

            >>> aux = g["AuxNamelist"]
            >>> aux["OUT1"] = eqmdsk.NamelistBlock({"ISHOT": 67590})
            >>> aux["OUT1"]["ISHOT"] = 67591
            >>> del aux["OUT1"]["ISHOT"]

        AuxNamelist cannot be assigned over or saved independently. Modify its
        blocks in place and save the owning GFile.

    COCOS detection, selection, and conversion:
        g.cocos is a read-only CocosResult with candidates, selected, and
        diagnostic. Detection uses signs in the current classical G-file
        fields. With one candidate, selected is that candidate; with zero or
        multiple candidates, selected is None. is_unique(), is_ambiguous(), and
        has_match() describe candidates only.

        _detect_cocos() returns a newly assembled result for the current field
        values without changing g.cocos. It is mainly useful for diagnostics
        after direct field edits.

        select_cocos(source) accepts only a value already in candidates. It
        changes selected alone: candidates, diagnostic, and every G-file field
        remain unchanged. It may be called repeatedly to choose another
        current candidate; selecting the already selected value is a no-op.

        to_cocos(to_cocos, from_cocos=None, inplace=True) performs the actual
        classical-field conversion. An explicit from_cocos is used directly
        and may be outside candidates, providing the deliberate fallback when
        automatic detection is unreliable, but it must still be a supported
        COCOS number. If from_cocos is omitted, selected is required.
        inplace=False returns an independent converted copy.

        After conversion, candidates and diagnostic are detected again from the
        converted fields, then selected is set to to_cocos independently.
        selected is therefore not required to belong to the new candidates.
        COCOS state is not serialized; reopening a saved file detects it again.
        Numerical extensions and AuxNamelist are not physically converted.

            >>> converted = g.to_cocos(11, from_cocos=5, inplace=False)
            >>> g.select_cocos(g.cocos.candidates[0])
            >>> g.to_cocos(11)

    Creating a new G-file:
        GFile.create(nw, nh) creates a pathless object. NW and NH remain required
        standard fields, but the factory fills them from ``nw`` and ``nh``; all
        other required fields initially appear as None. Fill all names returned
        by missing_fields() before saving. A minimal 3 by 2 file:

            >>> import numpy as np
            >>> g = eqmdsk.GFile.create(3, 2)
            >>> g["CASE"] = "minimal"
            >>> for name in (
            ...     "RDIM", "ZDIM", "RCENTR", "RLEFT", "ZMID", "RMAXIS",
            ...     "ZMAXIS", "SIMAG", "SIBRY", "BCENTR", "CURRENT",
            ... ):
            ...     g[name] = 1.0
            >>> for name in ("FPOL", "PRES", "FFPRIM", "PPRIME", "QPSI"):
            ...     g[name] = np.zeros(3)
            >>> g["PSIRZ"] = np.zeros((2, 3))
            >>> g["NBBBS"] = 0
            >>> g["LIMITR"] = 0
            >>> for name in ("RBBBS", "ZBBBS", "RLIM", "ZLIM"):
            ...     g[name] = np.empty(0)
            >>> g.save("created.g")

        A pathless object requires save(path); save() without a target raises
        ValidationError. A loaded object can use save() for its original path
        or save(path) for another file. Writing preserves parsed semantics, not
        original spacing, comments, or numeric formatting.

    Path properties:
        filename, path, and abspath are read-only str properties. filename is
        the base name, path preserves the supplied relative/absolute form, and
        abspath is normalized to an absolute path. All three are empty on a
        newly created pathless object.

    Documentation:
        https://github.com/hurd-git/eqmdsk/blob/main/docs/gfile.md
    """
    _core_cls = _GFileCore
    _display_name = "GFile"
    _read_only_fields = ("RGRID", "ZGRID")

    def __init__(self, path: _PathInput) -> None:
        super().__init__(path)

    def __getitem__(self, name: str, /) -> Any:
        """Return a typed G-file field or the attached ``AuxNamelist``.

        Args:
            name: Public EFIT field name, or ``"AuxNamelist"``.

        Returns:
            The field value, NumPy array, or a ``Namelist`` for the auxiliary
            tail. Unknown names raise ``FieldError``.

        Example:
            ``nw = g["NW"]; out1 = g["AuxNamelist"].get("OUT1")``
        """
        return super().__getitem__(name)

    def copy(self) -> "GFile":
        """Return an independent deep copy of fields, arrays, and namelist.

        Returns:
            A new ``GFile`` retaining path metadata but no shared mutable data.
        """
        return super().copy()  # type: ignore[return-value]

    @property
    def cocos(self) -> CocosResult:
        """Return detected and selected COCOS metadata.

        Returns:
            A ``CocosResult`` containing candidates, selected source, and a
            diagnostic. Reading it does not change the G-file.
        """
        return self._core.cocos

    def _detect_cocos(self) -> CocosResult:
        """Detect COCOS from current fields without changing ``cocos``.

        Returns:
            A new result. A unique candidate is selected; zero or multiple
            candidates leave ``selected`` as ``None``.
        """
        return self._core._detect_cocos()

    @classmethod
    def create(cls, nw: int, nh: int) -> "GFile":
        """Create an empty G-file with an ``nw`` by ``nh`` grid.

        Args:
            nw: Number of radial grid points.
            nh: Number of vertical grid points.

        Returns:
            A pathless G-file with standard fields exposed as ``None`` until
            assigned. ``AuxNamelist`` is always an empty ``Namelist``.
        """
        core = _GFileCore.create(nw, nh)
        return cls._from_core(core)

    def _refresh(self) -> None:
        dict.clear(self)
        for name in self._core.keys():
            value = (
                None
                if self._core._is_missing_field(name)
                else self._value_for_display(name)
            )
            dict.__setitem__(self, name, value)
        for name in self._core._missing_fields():
            if name not in self:
                dict.__setitem__(self, name, None)
        for name in self._core._missing_optional_fields():
            if name not in self:
                dict.__setitem__(self, name, None)
        aux = getattr(self._core, "_aux_namelist", None)
        if aux is not None:
            dict.__setitem__(
                self,
                "AuxNamelist",
                Namelist._from_core(aux),
            )

    def select_cocos(self, source: int) -> None:
        """Select one source COCOS value from the current candidates.

        Args:
            source: Candidate COCOS number to select.

        Returns:
            None. Only ``cocos.selected`` changes; candidates and fields are
            untouched. A value outside candidates raises ``CocosError``.

        Example:
            ``g.select_cocos(g.cocos.candidates[0])``
        """
        self._core.select_cocos(source)

    def to_cocos(
        self,
        to_cocos: int,
        from_cocos: Optional[int] = None,
        inplace: bool = True,
    ) -> "GFile":
        """Convert classical G-file fields from one COCOS to another.

        Args:
            to_cocos: Supported destination COCOS number.
            from_cocos: Explicit source; otherwise the current selected source.
                It may be supplied when automatic detection is ambiguous.
            inplace: Modify this object when true; otherwise return a copy.

        Returns:
            The modified object, either this instance or an independent copy.
            After conversion ``selected`` is set to ``to_cocos`` and candidates
            are independently redetected.

        Raises:
            CocosError: If no source is supplied/selected or a COCOS is invalid.

        Example:
            ``converted = g.to_cocos(11, from_cocos=5, inplace=False)``
        """
        converted = self._core.to_cocos(to_cocos, from_cocos, inplace)
        if inplace:
            self._refresh()
            return self
        return type(self)._from_core(converted)

    def __setitem__(self, name: str, value: Any) -> None:
        if name == "AuxNamelist":
            raise TypeError("modify AuxNamelist blocks and fields in place")
        super().__setitem__(name, value)


class AFile(_PathFileMapping):
    """Read and write one EFIT A-file summary and diagnostic file.

    Parameters:
        path: Existing file name or path. Use ``AFile.create`` for a new file.

    Returns:
        A mapping of standard fields with editable ``header`` and ``footer``.

    Details:
        Header and footer are Python ``str`` values owned by the C++ object and
        are written with the standard records.

    Required fields:
        Control fields:
            SHOT, TIME, JFLAG, LFLAG, LIMLOC, MCO2V, MCO2R, QMFLAG,
            NLOLD, NLNEW.
        Equilibrium summary:
            CHISQ, RCENCM, BCENTR, IPMEAS, IPMHD, RCNTR, ZCNTR,
            AMINOR, ELONG, UTRI, LTRI, VOLUME, RCURRT, ZCURRT, QSTAR,
            BETAT, BETAP, LI, GAPIN, GAPOUT, GAPTOP, GAPBOT, Q95,
            VERTN, SHEAR, BPOLAV.
        Profiles and diagnostics:
            S1, S2, S3, QOUT, SEPIN, SEPOUT, SEPTOP, SIBDRY, AREA,
            WMHD, ERROR, ELONGM, QM, CDFLUX, ALPHA, RTTT, PSIREF,
            INDENT, RSEPS, ZSEPS, SEPEXP, SEPBOT, BTAXP, BTAXV, AQ1,
            AQ2, AQ3, DSEP, RM, ZM, PSIM, TAUMHD, BETAPD, BETATD,
            WDIA, DIAMAG, VLOOP, TAUDIA, QMERCI, TAVEM.
        Count-controlled arrays:
            NSILOP0, MAGPRI0, NFCOIL0, NESUM0, RCO2V, DCO2V, RCO2R,
            DCO2R, CSILOP, CMPR2, CCBRSP, ECCURT.

    Optional fields:
        Optional EFIT record groups may contain:
            PBINJ, RVSIN, ZVSIN, RVSOUT, ZVSOUT, VSURF, WPDOT,
            WBDOT, SLANTU, SLANTL, ZUPERTS, CHIPRE, CJOR95, PP95,
            DRSEP, YYY2, XNNC, CPROF, ORING, CJOR0, FEXPAN, QMIN,
            CHIMSE, SSI01, FEXPVS, SEPNOSE, SSI95, RHOQMIN, CJOR99,
            CJ1AVE, RMIDIN, RMIDOUT, PSURFA, PEAK, DMINUX, DMINLX,
            DOLUBAF, DOLUBAFM, DILUDOM, DILUDOMM, RATSOL, RVSIU,
            ZVSIU, RVSID, ZVSID, RVSOU, ZVSOU, RVSOD, ZVSOD, CONDNO,
            PSIN32, PSIN21, RQ32IN, RQ21TOP, CHILIBT, LI3, XBETAPR,
            TFLUX, TCHIMLS, TWAGAP.
        Other top-level fields are rejected with FieldError. header and footer
        are editable file-level text properties and do not appear in keys().

    Reading, editing, and saving:
        Constructing AFile reads the complete existing file. Fields use the
        dictionary-style API; header and footer use properties:

            >>> a = eqmdsk.AFile("a067590.03300")
            >>> shot = a["SHOT"]
            >>> betap = a["BETAP"]
            >>> chord_lengths = a["RCO2V"]
            >>> a["BETAP"] = 0.25
            >>> a.header = a.header.replace("01-Jan-00", "02-Feb-00")
            >>> a.footer = "producer-specific text\\n"
            >>> a.save("a.modified")

        copy() deep-copies fields, arrays, header, footer, and path metadata.
        Deleting a required field restores it as None and reports it through
        missing_fields(); deleting an optional field removes it.

        MCO2V, MCO2R, NSILOP0, MAGPRI0, NFCOIL0, and NESUM0 determine
        associated array lengths. If a count and its arrays disagree, save()
        raises ValidationError. Real fields accept Python integers as floats;
        integer control fields reject floating-point values.

    Creating a new A-file:
        AFile.create() returns a pathless object whose standard fields are None.
        One practical way to create a complete object is to copy fields from a
        compatible source and then edit them:

            >>> source = eqmdsk.AFile("a067590.03300")
            >>> created = eqmdsk.AFile.create()
            >>> for name in created.missing_fields():
            ...     created[name] = source[name]
            >>> for name in created.missing_optional_fields():
            ...     if name in source:
            ...         created[name] = source[name]
            >>> created["SHOT"] = 67591
            >>> created.save("created.a")

        Optional record groups must be provided continuously from the first
        group; isolated later groups are invalid. A new object requires an
        explicit save(path). Writing reconstructs normalized A-file records
        and preserves current header/footer text, not the source byte layout.

    Documentation:
        https://github.com/hurd-git/eqmdsk/blob/main/docs/afile.md
    """
    _core_cls = _AFileCore
    _display_name = "AFile"

    def __init__(self, path: _PathInput) -> None:
        super().__init__(path)

    def __getitem__(self, name: str, /) -> Any:
        """Return a typed A-file field.

        Args:
            name: Public EFIT A-file field name.

        Returns:
            A scalar, string, or NumPy array. Unknown names raise ``FieldError``.

        Example:
            ``chisq = a["CHISQ"]; labels = a["LIMLOC"]``
        """
        return super().__getitem__(name)

    def copy(self) -> "AFile":
        """Return an independent copy including header and footer text."""
        return super().copy()  # type: ignore[return-value]

    @property
    def header(self) -> str:
        """Editable text before the standard A-file records."""
        return self._core.header

    @header.setter
    def header(self, value: str) -> None:
        """Replace the file-level header with a Python string."""
        self._core.header = value

    @property
    def footer(self) -> str:
        """Editable text after the standard A-file records."""
        return self._core.footer

    @footer.setter
    def footer(self, value: str) -> None:
        """Replace the file-level footer with a Python string."""
        self._core.footer = value

    @classmethod
    def create(cls) -> "AFile":
        """Create an empty pathless A-file.

        Returns:
            A new object with standard fields missing and empty header/footer.
            Complete it and call ``save(path)``.
        """
        core = _AFileCore.create()
        return cls._from_core(core)


class SFile(_PathFileMapping):
    """Read and write one EFIT S-file with four numeric data columns.

    Parameters:
        path: Existing file name or path. Use ``SFile.create`` for new data.

    Returns:
        A mapping containing labels and equal-length ``X``, ``Y``, ``DX`` and
        ``DY`` NumPy arrays.

    Details:
        Array lengths and finite numeric values are validated when saving.

    Fields:
        Required data arrays:
            X, Y, DX, DY.
        Optional text labels:
            XLABEL, YLABEL, TITLE.
        No other top-level field names are valid. The optional text lines are
        represented directly by these fields; SFile has no separate raw header
        or footer.

    Reading, editing, and saving:
        Construction reads the complete existing file. The four data fields are
        C++-owned NumPy views and the labels are Python str values:

            >>> s = eqmdsk.SFile("s123456.01234")
            >>> x = s["X"]
            >>> title = s.get("TITLE")
            >>> s["Y"][0] = 42.0
            >>> s["TITLE"] = "modified"
            >>> s.save("s.modified")

        Element assignment through an array view immediately changes the SFile.
        Whole-array assignment replaces C++ storage. X, Y, DX, and DY must have
        equal lengths and contain finite real values when save() is called.
        Standard dtype is numpy.float64; integer arrays may convert to float64,
        while float32 and incompatible dtypes are rejected.

        copy() returns an independent object whose arrays do not share mutable
        storage. Canonical writing emits the optional labels and four numeric
        columns; interspersed text and original line formatting are not copied.

    Creating a new S-file:
        SFile.create(count) creates a pathless object with four required arrays
        of the requested length initially shown as None. Fill all four arrays;
        labels are optional:

            >>> import numpy as np
            >>> s = eqmdsk.SFile.create(2)
            >>> s["X"] = np.array([1.0, 2.0])
            >>> s["Y"] = np.array([3.0, 4.0])
            >>> s["DX"] = np.array([0.1, 0.1])
            >>> s["DY"] = np.array([0.2, 0.2])
            >>> s["TITLE"] = "created"
            >>> s.save("created.s")

        A new pathless object requires save(path). Loaded objects may use
        save() for their original path or save(path) for another file.

    Documentation:
        https://github.com/hurd-git/eqmdsk/blob/main/docs/sfile.md
    """
    _core_cls = _SFileCore
    _display_name = "SFile"

    def __init__(self, path: _PathInput) -> None:
        super().__init__(path)

    def __getitem__(self, name: str, /) -> Any:
        """Return a typed S-file label or data array.

        Args:
            name: One of the standard labels or data column names.

        Returns:
            A Python ``str`` label or C++-owned ``float64`` NumPy view.

        Example:
            ``x = s["X"]; s["DY"] = np.zeros(len(x))``
        """
        return super().__getitem__(name)

    def copy(self) -> "SFile":
        """Return an independent copy of labels and numeric arrays."""
        return super().copy()  # type: ignore[return-value]

    @classmethod
    def create(cls, count: int) -> "SFile":
        """Create a pathless S-file with four missing arrays.

        Args:
            count: Number of rows required for each data column.

        Returns:
            A new object whose labels and arrays must be filled before
            ``save(path)``.
        """
        core = _SFileCore.create(count)
        return cls._from_core(core)


class NamelistBlock(dict[str, Any]):
    """Dictionary-like C++ representation of one Fortran namelist block.

    Parameters:
        values: Optional mapping used to initialize public fields. Values are
            scalars, strings, string lists, or one-dimensional NumPy arrays.

    Returns:
        A block that can be assigned to a ``KFile`` or ``Namelist``.

    Details:
        Keys are normalized to uppercase. ``None`` and nested blocks are not
        valid field values; use a ``Namelist`` for nested blocks.

        Supported scalar values are int, float, bool, and str. Multiple integer
        values use a one-dimensional int64 NumPy array; real or mixed numeric
        values use float64; multiple strings use list[str]. Field names cannot
        be empty. Nested dictionaries, nested NamelistBlock objects, None, and
        multidimensional arrays are not valid field values.

        Assignment, update(), setdefault(), pop(), del, clear(), keys(),
        items(), values(), iteration, membership, and len() behave like their
        dictionary counterparts while forwarding every mutation to C++.
        Field names are case-insensitive and exposed in uppercase. copy()
        returns a deep copy with independent array storage.

    Example:
        ``block = NamelistBlock({"LIMITR": 60}); block["LIMITR"] = 61``

    Documentation:
        https://github.com/hurd-git/eqmdsk/blob/main/docs/python-api.md
    """

    def __init__(
        self, values: Optional[Mapping[str, Any]] = None
    ) -> None:
        dict.__init__(self)
        source: Any = values if values is not None else {}
        if isinstance(source, dict):
            self._core = _core._NamelistBlock()
            for name, value in source.items():
                if value is None:
                    raise TypeError("NamelistBlock fields cannot be None")
                self._core._assign(self._key(name), value)
        else:
            self._core = source
        self._refresh()

    def _refresh(self) -> None:
        dict.clear(self)
        for name in self._core.keys():
            dict.__setitem__(self, name, _field_value(self._core, name))

    def _key(self, name: str) -> str:
        return name.upper()

    def keys(self) -> List[str]:  # type: ignore[override]
        """Return normalized uppercase field names."""
        return list(dict.keys(self))

    def items(self) -> List[Tuple[str, Any]]:  # type: ignore[override]
        """Return a snapshot of ``(field, value)`` pairs."""
        return list(dict.items(self))

    def values(self) -> List[Any]:  # type: ignore[override]
        """Return a snapshot of field values."""
        return list(dict.values(self))

    def __iter__(self) -> Iterator[str]:
        """Iterate over normalized uppercase field names."""
        return dict.__iter__(self)

    def __len__(self) -> int:
        """Return the number of fields in this block."""
        return dict.__len__(self)

    def __contains__(self, name: object) -> bool:
        return isinstance(name, str) and (
            dict.__contains__(self, self._key(name))
            or self._core.__contains__(self._key(name))
        )

    def __getitem__(self, name: str) -> Any:
        """Return a field value, raising ``FieldError`` for unknown names."""
        key = self._key(name)
        if dict.__contains__(self, key):
            return dict.__getitem__(self, key)
        return _field_value(self._core, key)

    def get(self, name: str, default: Any = None) -> Any:
        if name not in self:
            return default
        return self[name]

    def __setitem__(self, name: str, value: Any) -> None:
        """Create or replace one field through the C++ value model.

        Args:
            name: Field name; matching is case-insensitive.
            value: Supported scalar, string/list, or NumPy array value.

        Returns:
            None. The field is immediately owned by the C++ block.
        """
        key = self._key(name)
        if value is None:
            raise TypeError("NamelistBlock fields cannot be None")
        if self._core.__contains__(key):
            self._core[key] = value
        else:
            self._core._assign(key, value)
        dict.__setitem__(self, key, _field_value(self._core, key))

    def update(
        self,
        other: Any = (),
        /,
        **kwargs: Any,
    ) -> None:
        """Create or replace multiple fields through C++.

        Args:
            other: Mapping or iterable of ``(name, value)`` pairs.
            kwargs: Additional field assignments.

        Returns:
            None. Values are validated as they are assigned.
        """
        items: Iterable[Tuple[str, Any]]
        if hasattr(other, "keys"):
            items = ((key, other[key]) for key in other.keys())
        else:
            items = other
        for key, value in items:
            self[key] = value
        for key, value in kwargs.items():
            self[key] = value

    def setdefault(self, name: str, default: Any = None, /) -> Any:
        """Return an existing value or insert and return ``default``."""
        if name in self:
            return self[name]
        self[name] = default
        return self[name]

    def __ior__(self, other: Any) -> "NamelistBlock":
        self.update(other)
        return self

    def __delitem__(self, name: str) -> None:
        """Delete one field; missing names raise ``KeyError``."""
        key = self._key(name)
        if key not in self:
            raise KeyError(name)
        self._core._erase(key)
        dict.__delitem__(self, key)

    def clear(self) -> None:
        for name in list(self.keys()):
            del self[name]

    def pop(self, name: str, *args: Any) -> Any:
        """Remove and return one field, optionally returning a default."""
        if len(args) > 1:
            raise TypeError(f"pop expected at most 2 arguments, got {len(args) + 1}")
        default = args[0] if args else _MISSING
        key = self._key(name)
        if key not in self:
            if default is _MISSING:
                raise KeyError(name)
            return default
        value = self[key]
        del self[key]
        return value

    def popitem(self) -> Tuple[str, Any]:
        """Remove and return the last ``(field, value)`` pair."""
        if not self:
            raise KeyError("empty NamelistBlock")
        key, value = next(reversed(self.items()))
        del self[key]
        return key, value

    def __repr__(self) -> str:
        return dict.__repr__(self)

    def copy(self) -> "NamelistBlock":
        """Return an independent C++-owned deep copy of this namelist block."""
        return type(self)(self._core.copy())


class Namelist(_FileMapping):
    """Path-independent mapping of Fortran namelist blocks.

    Returns:
        An initially empty mapping from uppercase block names to
        ``NamelistBlock`` objects.

    Details:
        A plain ``Namelist`` has no path or public ``save`` method. It is used
        as ``GFile["AuxNamelist"]`` or copied into a ``KFile``.

        Block names are case-insensitive and exposed in uppercase. Every value
        must be a NamelistBlock; assigning a scalar or ordinary dictionary as a
        block is an error. Assigning a block copies its C++ fields into storage
        owned by this Namelist.

        Use mapping assignment, update(), pop(), del, clear(), keys(), items(),
        values(), membership, iteration, and copy() to manage blocks:

            >>> n = eqmdsk.Namelist()
            >>> n["OUT1"] = eqmdsk.NamelistBlock({"ISHOT": 67590})
            >>> n["OUT1"]["ISHOT"] = 67591
            >>> copied = n.copy()
            >>> del n["OUT1"]

        A standalone Namelist is an in-memory object and has no save() method.
        Copy its blocks into a KFile, or modify the Namelist owned by a GFile
        and save that owning file.

    Documentation:
        https://github.com/hurd-git/eqmdsk/blob/main/docs/python-api.md
    """
    _core_cls = _core._Namelist
    _case_insensitive = True
    _display_name = "Namelist"

    def __init__(self, *, _core_object: Any = None) -> None:
        if _core_object is None:
            _core_object = _core._Namelist.create()
        super().__init__(_core_object=_core_object)

    @classmethod
    def _from_core(cls, core_object: Any) -> "Namelist":
        return super()._from_core(core_object)

    def _refresh(self) -> None:
        dict.clear(self)
        for name in self._core.keys():
            dict.__setitem__(
                self,
                name,
                NamelistBlock(self._core[name]),
            )

    def keys(self) -> List[str]:  # type: ignore[override]
        """Return normalized uppercase block names in stored order."""
        return super().keys()

    def items(self) -> List[Tuple[str, "NamelistBlock"]]:  # type: ignore[override]
        """Return a snapshot of ``(block_name, block)`` pairs."""
        return super().items()

    def values(self) -> List["NamelistBlock"]:  # type: ignore[override]
        """Return a snapshot of block objects in stored order."""
        return super().values()

    def copy(self) -> "Namelist":
        """Return an independent deep copy of all blocks and values."""
        return super().copy()  # type: ignore[return-value]

    def __setitem__(self, name: str, value: NamelistBlock) -> None:
        """Create or replace one namelist block.

        Args:
            name: Block name, normalized to uppercase.
            value: C++-backed ``NamelistBlock`` whose fields are copied.

        Returns:
            None. Non-block values raise ``TypeError``.

        Example:
            ``n["OUT1"] = NamelistBlock({"ISHOT": 67590})``
        """
        key = self._key(name)
        if not isinstance(value, NamelistBlock):
            raise TypeError("Namelist values must be NamelistBlock instances")
        if key in self:
            self._core._erase_block(key)
            dict.__delitem__(self, key)
        self._core._assign_block(key)
        block = NamelistBlock(self._core[key])
        for field_name, field_value in value.items():
            if field_value is None:
                continue
            self._core[key]._assign(field_name, field_value)
            dict.__setitem__(
                block, field_name, _field_value(self._core[key], field_name)
            )
        dict.__setitem__(self, key, block)

    def __getitem__(self, name: str) -> NamelistBlock:
        """Return the named block.

        Args:
            name: Case-insensitive namelist block name.

        Returns:
            The C++-backed ``NamelistBlock`` attached to this namelist.

        Example:
            ``limitr = n["IN1"]["LIMITR"]``
        """
        key = self._key(name)
        if dict.__contains__(self, key):
            return dict.__getitem__(self, key)
        return super().__getitem__(key)

    def __delitem__(self, name: str) -> None:
        """Delete one block; missing names raise ``KeyError``."""
        key = self._key(name)
        if key not in self:
            raise KeyError(name)
        self._core._erase_block(key)
        dict.__delitem__(self, key)

    def pop(self, name: str, *args: Any) -> Any:
        """Remove and return one block, optionally returning a default."""
        if len(args) > 1:
            raise TypeError(f"pop expected at most 2 arguments, got {len(args) + 1}")
        key = self._key(name)
        if key not in self:
            if args:
                return args[0]
            raise KeyError(name)
        value = self[key]
        del self[key]
        return value

    def clear(self) -> None:
        for name in list(self.keys()):
            del self[name]


class KFile(Namelist):
    """Read and write a file-backed EFIT Fortran namelist.

    Parameters:
        path: Existing K-file name or path. Use ``KFile.create`` for a new file.

    Returns:
        A ``Namelist`` mapping with read-only ``filename``, ``path`` and
        ``abspath`` properties plus ``save``.

    Details:
        ``KFile`` inherits all block operations from ``Namelist``; blocks and
        fields are held by C++ and written in normalized namelist syntax.

    Block and field rules:
        K-file has no universal required-field table or block whitelist. IN1
        and IN2 are common names, but the accepted blocks and variables depend
        on the EFIT version and run configuration. Block and field names are
        case-insensitive and are exposed in uppercase.

        The outer mapping contains NamelistBlock values. A missing block is not
        created implicitly; assign a NamelistBlock before writing its fields:

            >>> k = eqmdsk.KFile("k067590.03300")
            >>> limitr = k["IN1"]["LIMITR"]
            >>> k["IN1"]["LIMITR"] = 61
            >>> k["IN2"] = eqmdsk.NamelistBlock()
            >>> k["IN2"]["ITIME"] = 2

        NamelistBlock accepts int, float, bool, str, one-dimensional integer or
        float arrays, and lists of strings. It rejects None, empty field names,
        nested mappings/blocks, multidimensional arrays, and unsupported
        objects. A block deliberately has no fixed field whitelist so that
        variables from different EFIT versions remain usable.

    Mapping operations and saving:
        Blocks and fields can be added, replaced, updated, popped, or deleted:

            >>> k["IN2"] = eqmdsk.NamelistBlock({"VALUE": 3.5})
            >>> k["IN2"].update({"ITIME": 2, "ISHOT": 67590})
            >>> del k["IN2"]["VALUE"]
            >>> copied = k.copy()
            >>> k.save("k.modified")

        copy() deep-copies blocks, values, arrays, and path metadata. save()
        writes stable Fortran namelist syntax using &BLOCK, assignments, and /.
        Comments, text outside blocks, repeated-assignment history, original
        capitalization, and source formatting are not preserved.

    Creating a new K-file:
        KFile.create() returns an empty pathless K-file and does not add IN1 or
        any other predefined block:

            >>> k = eqmdsk.KFile.create()
            >>> k["IN1"] = eqmdsk.NamelistBlock()
            >>> k["IN1"].update({"LIMITR": 60, "ITIME": 2})
            >>> k["IN2"] = eqmdsk.NamelistBlock({"VALUE": 3.5})
            >>> k.save("created.k")

        A new object requires save(path). Loaded objects may use save() for the
        original path or save(path) for another file.

    Relationship to Namelist:
        KFile inherits the path-independent Namelist mapping and adds path and
        save behavior. GFile["AuxNamelist"] uses the same Namelist and
        NamelistBlock operations, but it is owned by its GFile and must be
        saved through that GFile.

    Documentation:
        https://github.com/hurd-git/eqmdsk/blob/main/docs/kfile.md
    """
    _display_name = "KFile"

    def __init__(self, path: _PathInput) -> None:
        super().__init__(_core_object=_core.KFile(os.fspath(path)))

    def copy(self) -> "KFile":
        """Return an independent copy of blocks, values, and path metadata."""
        return super().copy()  # type: ignore[return-value]

    @property
    def filename(self) -> str:
        """Return the read-only base file name as ``str``."""
        return self._core.filename

    @property
    def path(self) -> str:
        """Return the read-only input path as ``str``."""
        return self._core.path

    @property
    def abspath(self) -> str:
        """Return the read-only normalized absolute path as ``str``."""
        return self._core.abspath

    def save(self, path: Optional[_PathInput] = None) -> None:
        """Write normalized namelist text to ``path`` or the original path.

        Args:
            path: Optional output path. Pathless objects require this argument.

        Returns:
            None. Missing required fields and invalid blocks raise validation
            errors before writing.

        Details:
            The writer emits normalized Fortran namelist syntax rather than a
            byte-for-byte reproduction of the source.

        Example:
            ``k.save()`` or ``k.save("copy.k")``
        """
        if path is None:
            self._core.save()
        else:
            self._core.save(os.fspath(path))

    @classmethod
    def create(cls) -> "KFile":
        """Create an empty pathless K-file.

        Returns:
            A new K-file with no blocks. Add blocks with mapping assignment,
            then call ``save(path)``.
        """
        core = _core.KFile.create()
        return cls._from_core(core)


# Keep constructor help on the class documentation without duplicating text.
for _documented_type in (GFile, AFile, SFile, KFile, Namelist, NamelistBlock):
    _documented_type.__init__.__doc__ = _documented_type.__doc__
del _documented_type


__all__ = [
    "AFile",
    "CocosError",
    "CocosResult",
    "Error",
    "FieldError",
    "GFile",
    "IOError",
    "KFile",
    "Namelist",
    "NamelistBlock",
    "ParseError",
    "SFile",
    "ValidationError",
    "__version__",
]
