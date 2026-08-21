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
    converted: eqmdsk.GFile = g.to_cocos(
        to_cocos=11, from_cocos=5, inplace=False
    )
    result: eqmdsk.CocosResult = g.cocos
    target: str = g.filename
    _ = (case, nw, current, psi, converted, result, target)


def check_afile(a: eqmdsk.AFile) -> None:
    shot: int = a["SHOT"]
    flag: str = a["LIMLOC"]
    chisq: float = a["CHISQ"]
    chord: NDArray[np.float64] = a["RCO2V"]
    _ = (shot, flag, chisq, chord)


def check_kfile(k: eqmdsk.KFile) -> None:
    section: eqmdsk.KSection = k["IN1"]
    limitr: int = section["LIMITR"]
    value: FieldValue = section["BTOR"]
    _ = (limitr, value)


def check_sfile(s: eqmdsk.SFile) -> None:
    title: str = s["TITLE"]
    x: NDArray[np.float64] = s["X"]
    s.write(Path("s.output"))
    _ = (title, x)
