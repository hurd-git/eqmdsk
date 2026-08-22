"""Lightweight EFIT file I/O backed by C++.

The public classes are debugger-friendly ``dict`` facades.  C++ owns the
actual fields, namelist blocks, parsing state, and serialization; Python mapping
operations forward mutations to that core and mirror values for inspection.
Mutable NumPy values remain zero-copy views of C++ storage.
"""

from __future__ import annotations

import os
from typing import Any, Iterable, Iterator, List, Optional, Tuple, Type

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


def _canonical_block_key(name: str) -> str:
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
    _read_only_fields: Tuple[str, ...] = ()

    def __init__(
        self,
        path: Any = None,
        *,
        _core_object: Any = None,
    ) -> None:
        dict.__init__(self)
        if _core_object is None:
            if path is None:
                raise TypeError("a file path is required")
            _core_object = self._core_cls(os.fspath(path))
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
        return dict.__contains__(self, self._key(name)) or self._core.__contains__(
            self._key(name)
        )

    def __getitem__(self, name: str) -> Any:
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
        self._erase_field(name)

    def clear(self) -> None:
        for name in list(self.keys()):
            del self[name]

    def pop(self, name: str, *args: Any) -> Any:
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
        if not self:
            raise KeyError("popitem(): mapping is empty")
        key = next(reversed(self.keys()))
        value = self[key]
        del self[key]
        return key, value

    def missing_fields(self) -> List[str]:
        return self._core._missing_fields()

    def missing_optional_fields(self) -> List[str]:
        return self._core._missing_optional_fields()

    def __getattr__(self, name: str) -> Any:
        # COCOS and format-specific read-only helpers remain owned by C++.
        return getattr(self._core, name)

    def __repr__(self) -> str:
        return f"{self._display_name}({dict.__repr__(self)})"

    def copy(self) -> "_FileMapping":
        """Return an independent C++-owned copy of this mapping."""
        return type(self)._from_core(self._core.copy())


class _PathFileMapping(_FileMapping):
    """Mapping facade for a file object that owns a path."""

    @property
    def filename(self) -> str:
        return self._core.filename

    @property
    def path(self) -> str:
        return self._core.path

    @property
    def abspath(self) -> str:
        return self._core.abspath

    def save(self, path: Optional[Any] = None) -> None:
        if path is None:
            self._core.save()
        else:
            self._core.save(os.fspath(path))


class GFile(_PathFileMapping):
    _core_cls = _GFileCore
    _display_name = "GFile"
    _read_only_fields = ("RGRID", "ZGRID")

    def __init__(self, path: Any) -> None:
        super().__init__(path)

    @classmethod
    def create(cls, nw: int, nh: int) -> "GFile":
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
            raise TypeError("modify AuxNamelist blocks and fields in place")
        super().__setitem__(name, value)


class AFile(_PathFileMapping):
    _core_cls = _AFileCore
    _display_name = "AFile"

    def __init__(self, path: Any) -> None:
        super().__init__(path)

    @property
    def header(self) -> str:
        return self._core.header

    @header.setter
    def header(self, value: str) -> None:
        self._core.header = value

    @property
    def footer(self) -> str:
        return self._core.footer

    @footer.setter
    def footer(self, value: str) -> None:
        self._core.footer = value

    @classmethod
    def create(cls) -> "AFile":
        core = _AFileCore.create()
        return cls._from_core(core)


class SFile(_PathFileMapping):
    _core_cls = _SFileCore
    _display_name = "SFile"

    def __init__(self, path: Any) -> None:
        super().__init__(path)

    @classmethod
    def create(cls, count: int) -> "SFile":
        core = _SFileCore.create(count)
        return cls._from_core(core)


class NamelistBlock(dict):
    """Debugger-visible mapping for one Fortran namelist block."""

    def __init__(self, core_block: Any = None) -> None:
        dict.__init__(self)
        source = core_block if core_block is not None else {}
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
        return list(dict.keys(self))

    def items(self) -> List[Tuple[str, Any]]:  # type: ignore[override]
        return list(dict.items(self))

    def values(self) -> List[Any]:  # type: ignore[override]
        return list(dict.values(self))

    def __contains__(self, name: object) -> bool:
        return isinstance(name, str) and (
            dict.__contains__(self, self._key(name))
            or self._core.__contains__(self._key(name))
        )

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

    def __ior__(self, other: Any) -> "NamelistBlock":
        self.update(other)
        return self

    def __delitem__(self, name: str) -> None:
        key = self._key(name)
        if key not in self:
            raise KeyError(name)
        self._core._erase(key)
        dict.__delitem__(self, key)

    def clear(self) -> None:
        for name in list(self.keys()):
            del self[name]

    def pop(self, name: str, *args: Any) -> Any:
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
        if not self:
            raise KeyError("empty NamelistBlock")
        key, value = next(reversed(self.items()))
        del self[key]
        return key, value

    def __repr__(self) -> str:
        return dict.__repr__(self)

    def copy(self) -> "NamelistBlock":
        """Return an independent copy of this namelist block."""
        return type(self)(self._core.copy())


class Namelist(_FileMapping):
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

    def __setitem__(self, name: str, value: Any) -> None:
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

    def __getitem__(self, name: str) -> Any:
        key = self._key(name)
        if dict.__contains__(self, key):
            return dict.__getitem__(self, key)
        return super().__getitem__(key)

    def __delitem__(self, name: str) -> None:
        key = self._key(name)
        if key not in self:
            raise KeyError(name)
        self._core._erase_block(key)
        dict.__delitem__(self, key)

    def pop(self, name: str, *args: Any) -> Any:
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
    _display_name = "KFile"

    def __init__(self, path: Any) -> None:
        super().__init__(_core_object=_core.KFile(os.fspath(path)))

    @property
    def filename(self) -> str:
        return self._core.filename

    @property
    def path(self) -> str:
        return self._core.path

    @property
    def abspath(self) -> str:
        return self._core.abspath

    def save(self, path: Optional[Any] = None) -> None:
        if path is None:
            self._core.save()
        else:
            self._core.save(os.fspath(path))

    @classmethod
    def create(cls) -> "KFile":
        core = _core.KFile.create()
        return cls._from_core(core)

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
