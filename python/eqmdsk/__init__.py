"""Lightweight EFIT file I/O backed by C++.

The public file classes are real ``dict`` subclasses.  Their entries are a
debugger-friendly snapshot of the C++ mapping; mutable NumPy values remain
zero-copy views, while scalar assignments are forwarded to the C++ owner.
"""

from __future__ import annotations

import os
import sys
from importlib.machinery import EXTENSION_SUFFIXES
from pathlib import Path
from typing import Any, Iterable, Iterator, List, Optional, Tuple, Type

# PyCharm may put the checkout's ``python`` source root before site-packages.
# In that development-only layout, find the compiled extension installed in the
# same interpreter environment so importing the source facade remains useful.
_source_package = Path(__file__).resolve().parent
_candidate = None
for _entry in sys.path:
    if not _entry:
        continue
    _candidate = (Path(_entry) / "eqmdsk").resolve()
    if _candidate == _source_package or not _candidate.is_dir():
        continue
    if any((_candidate / f"_core{suffix}").is_file()
           for suffix in EXTENSION_SUFFIXES):
        if os.fspath(_candidate) not in __path__:
            __path__.append(os.fspath(_candidate))
        break
del _candidate, _entry, _source_package

from . import _core

_AFileCore = _core.AFile
CocosError = _core.CocosError
CocosResult = _core.CocosResult
Error = _core.Error
FieldError = _core.FieldError
_GFileCore = _core.GFile
IOError = _core.IOError
ParseError = _core.ParseError
_SFileCore = _core.SFile
ValidationError = _core.ValidationError
__version__ = _core.__version__


def _canonical_section_key(name: str) -> str:
    return name.upper()


def _field_value(core: Any, name: str) -> Any:
    """Convert one core value while preserving the public error contract."""
    try:
        return core[name]
    except UnicodeDecodeError as exc:
        raise Error(f"field {name!r} contains invalid UTF-8 text") from exc


_MISSING = object()


class _FileMapping(dict):
    """Debugger-visible dict facade over one C++ file object."""

    _core_cls: Type[Any]
    _case_insensitive = False
    _display_name = "EFITFile"

    def __init__(self, filename: Any = None, *, _core_object: Any = None) -> None:
        dict.__init__(self)
        if _core_object is None:
            _core_object = self._core_cls(os.fspath(filename))
        self._core = _core_object
        self._refresh()

    @classmethod
    def _from_core(cls, core_object: Any) -> "_FileMapping":
        result = cls.__new__(cls)
        dict.__init__(result)
        result._core = core_object
        result._refresh()
        return result

    def _key(self, name: str) -> str:
        if self._case_insensitive:
            return _canonical_section_key(name)
        return name

    def _refresh(self) -> None:
        dict.clear(self)
        for name in self._core.keys():
            dict.__setitem__(self, name, _field_value(self._core, name))

    @property
    def filename(self) -> str:
        return self._core.filename

    def keys(self) -> List[str]:  # type: ignore[override]
        return list(dict.keys(self))

    def items(self) -> List[Tuple[str, Any]]:  # type: ignore[override]
        return list(dict.items(self))

    def values(self) -> List[Any]:  # type: ignore[override]
        return list(dict.values(self))

    def __iter__(self) -> Iterator[str]:
        return dict.__iter__(self)

    def __contains__(self, name: object) -> bool:
        if not isinstance(name, str):
            return False
        return dict.__contains__(self, self._key(name))

    def __getitem__(self, name: str) -> Any:
        key = self._key(name)
        if dict.__contains__(self, key):
            return dict.__getitem__(self, key)
        # Let the C++ implementation provide the established FieldError.
        return _field_value(self._core, key)

    def get(self, name: str, default: Any = None) -> Any:
        if name not in self:
            return default
        return self[name]

    def __setitem__(self, name: str, value: Any) -> None:
        key = self._key(name)
        self._core[key] = value
        dict.__setitem__(self, key, _field_value(self._core, key))

    def update(
        self,
        other: Any = (),
        /,
        **kwargs: Any,
    ) -> None:
        """Update existing fields through the core mapping."""
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
        if name in self:
            return self[name]
        self[name] = default
        return self[name]

    def __ior__(self, other: Any) -> "_FileMapping":
        self.update(other)
        return self

    def __delitem__(self, name: str) -> None:
        raise TypeError("EFIT fields cannot be removed")

    def clear(self) -> None:
        raise TypeError("EFIT fields cannot be removed")

    def pop(self, name: str, *args: Any) -> Any:
        if len(args) > 1:
            raise TypeError(f"pop expected at most 2 arguments, got {len(args) + 1}")
        default = args[0] if args else _MISSING
        if name in self:
            raise TypeError("EFIT fields cannot be removed")
        if default is _MISSING:
            raise KeyError(name)
        return default

    def popitem(self) -> Tuple[str, Any]:
        raise TypeError("EFIT fields cannot be removed")

    def write(self, path: Optional[Any] = None) -> None:
        if path is None:
            self._core.write()
        else:
            self._core.write(os.fspath(path))

    def __getattr__(self, name: str) -> Any:
        # COCOS and format-specific read-only helpers remain owned by C++.
        return getattr(self._core, name)

    def __repr__(self) -> str:
        return f"{self._display_name}({dict.__repr__(self)})"


class GFile(_FileMapping):
    _core_cls = _GFileCore
    _display_name = "GFile"

    def __init__(self, filename: Any) -> None:
        super().__init__(filename)

    def _refresh(self) -> None:
        dict.clear(self)
        for name in self._core.keys():
            dict.__setitem__(self, name, _field_value(self._core, name))
        aux = getattr(self._core, "_aux_namelist", None)
        if aux is not None:
            dict.__setitem__(self, "AuxNamelist", _AuxNamelist._from_core(aux))

    def select_cocos(self, source: int) -> None:
        self._core.select_cocos(source)

    def to_cocos(
        self,
        to_cocos: int,
        from_cocos: Optional[int] = None,
        inplace: bool = True,
    ) -> "GFile":
        converted = self._core.to_cocos(to_cocos, from_cocos, inplace)
        if inplace:
            self._refresh()
            return self
        return type(self)._from_core(converted)

    def __setitem__(self, name: str, value: Any) -> None:
        if name == "AuxNamelist":
            raise TypeError("modify AuxNamelist sections and fields in place")
        super().__setitem__(name, value)


class AFile(_FileMapping):
    _core_cls = _AFileCore
    _display_name = "AFile"

    def __init__(self, filename: Any) -> None:
        super().__init__(filename)


class SFile(_FileMapping):
    _core_cls = _SFileCore
    _display_name = "SFile"

    def __init__(self, filename: Any) -> None:
        super().__init__(filename)


class KSection(dict):
    """Debugger-visible dict facade for one K-file namelist section."""

    def __init__(self, core_section: Any) -> None:
        dict.__init__(self)
        self._core = core_section
        self._refresh()

    def _refresh(self) -> None:
        dict.clear(self)
        for name in self._core.keys():
            dict.__setitem__(self, name, _field_value(self._core, name))

    def _key(self, name: str) -> str:
        return name.upper()

    def keys(self) -> List[str]:  # type: ignore[override]
        return list(dict.keys(self))

    def items(self) -> List[Tuple[str, Any]]:  # type: ignore[override]
        return list(dict.items(self))

    def values(self) -> List[Any]:  # type: ignore[override]
        return list(dict.values(self))

    def __contains__(self, name: object) -> bool:
        return isinstance(name, str) and self._core.__contains__(self._key(name))

    def __getitem__(self, name: str) -> Any:
        key = self._key(name)
        if dict.__contains__(self, key):
            return dict.__getitem__(self, key)
        return _field_value(self._core, key)

    def get(self, name: str, default: Any = None) -> Any:
        if name not in self:
            return default
        return self[name]

    def __setitem__(self, name: str, value: Any) -> None:
        key = self._key(name)
        self._core[key] = value
        dict.__setitem__(self, key, _field_value(self._core, key))

    def update(
        self,
        other: Any = (),
        /,
        **kwargs: Any,
    ) -> None:
        """Update existing fields through the core mapping."""
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
        if name in self:
            return self[name]
        self[name] = default
        return self[name]

    def __ior__(self, other: Any) -> "KSection":
        self.update(other)
        return self

    def __delitem__(self, name: str) -> None:
        raise TypeError("K-file fields cannot be removed")

    def clear(self) -> None:
        raise TypeError("K-file fields cannot be removed")

    def pop(self, name: str, *args: Any) -> Any:
        if len(args) > 1:
            raise TypeError(f"pop expected at most 2 arguments, got {len(args) + 1}")
        default = args[0] if args else _MISSING
        if name in self:
            raise TypeError("K-file fields cannot be removed")
        if default is _MISSING:
            raise KeyError(name)
        return default

    def popitem(self) -> Tuple[str, Any]:
        raise TypeError("K-file fields cannot be removed")

    def __repr__(self) -> str:
        return dict.__repr__(self)


class KFile(_FileMapping):
    _core_cls = _core.KFile
    _case_insensitive = True
    _display_name = "KFile"

    def __init__(self, filename: Any) -> None:
        super().__init__(filename)

    def _refresh(self) -> None:
        dict.clear(self)
        for name in self._core.keys():
            dict.__setitem__(
                self,
                name,
                KSection(self._core[name]),
            )

    def __setitem__(self, name: str, value: Any) -> None:
        raise TypeError("assign variables through kfile[section][name]")


class _AuxNamelist(KFile):
    _display_name = "AuxNamelist"

    def write(self, path: Optional[Any] = None) -> None:
        raise TypeError("write the owning GFile instead of AuxNamelist")


__all__ = [
    "AFile",
    "CocosError",
    "CocosResult",
    "Error",
    "FieldError",
    "GFile",
    "IOError",
    "KFile",
    "KSection",
    "ParseError",
    "SFile",
    "ValidationError",
    "__version__",
]
