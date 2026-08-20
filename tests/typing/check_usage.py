from pathlib import Path
from typing import List, Union

import numpy as np
from numpy.typing import NDArray

import eqmdsk


FieldValue = Union[
    bool,
    int,
    float,
    str,
    List[str],
    NDArray[np.int64],
    NDArray[np.float64],
]


def check_gfile(g: eqmdsk.GFile) -> None:
    case: str = g["CASE"]
    nw: int = g["NW"]
    current: float = g["CURRENT"]
    psi: NDArray[np.float64] = g["PSIRZ"]
    converted: eqmdsk.GFile = g.to_cocos(11, inplace=False)
    result: eqmdsk.CocosResult = g.cocos
    target: Path = g.filename
    _ = (case, nw, current, psi, converted, result, target)


def check_afile(a: eqmdsk.AFile) -> None:
    shot: int = a["SHOT"]
    flag: str = a["LIMLOC"]
    chisq: float = a["CHISQ"]
    chord: NDArray[np.float64] = a["RCO2V"]
    count: int = a.optional_record_count
    raw: bytes = a.footer
    _ = (shot, flag, chisq, chord, count, raw)


def check_kfile(k: eqmdsk.KFile) -> None:
    value: FieldValue = k["DEVICE_DEFINED_NAME"]
    sections: List[eqmdsk.NamelistSection] = k.sections
    entry: eqmdsk.NamelistEntry = k.entry("IN1", "BTOR")
    values: List[eqmdsk.NamelistValue] = entry.values
    scalar: Union[None, int, float, bool, str, complex] = values[0].value
    k.set("IN1", "BTOR", [eqmdsk.NamelistValue.real(-2.1)])
    _ = (value, sections, scalar)


def check_sfile(s: eqmdsk.SFile) -> None:
    title: str = s["TITLE"]
    x: NDArray[np.float64] = s["X"]
    s.write(Path("s.output"))
    _ = (title, x)


def check_rejected_calls(
    g: eqmdsk.GFile,
    a: eqmdsk.AFile,
    s: eqmdsk.SFile,
    fields: eqmdsk.FieldMap,
) -> None:
    # These ignores are expected-error assertions: strict mypy reports an
    # unused ignore if a future broad overload accidentally accepts the call.
    g["NW"] = "wrong"  # type: ignore[call-overload]
    g["CURRENT"] = "wrong"  # type: ignore[call-overload]
    g["PSIRZ"] = "wrong"  # type: ignore[call-overload]
    a["SHOT"] = "wrong"  # type: ignore[call-overload]
    s["TITLE"] = 1  # type: ignore[call-overload]
    fields.contains(name="CURRENT")  # type: ignore[call-arg]
