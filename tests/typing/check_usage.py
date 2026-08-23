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
    aux: eqmdsk.Namelist = g["AuxNamelist"]
    converted: eqmdsk.GFile = g.to_cocos(
        to_cocos=11, from_cocos=5, inplace=False
    )
    result: eqmdsk.CocosResult = g.cocos
    detected: eqmdsk.CocosResult = g._detect_cocos()
    target: str = g.filename
    relative_path: str = g.path
    absolute_path: str = g.abspath
    g_copy: eqmdsk.GFile = g.copy()
    _ = (case, nw, current, psi, aux, converted, result, detected, target,
         relative_path, absolute_path, g_copy)


def check_afile(a: eqmdsk.AFile) -> None:
    shot: int = a["SHOT"]
    flag: str = a["LIMLOC"]
    chisq: float = a["CHISQ"]
    chord: NDArray[np.float64] = a["RCO2V"]
    header: str = a.header
    footer: str = a.footer
    a.header = header
    a.footer = footer
    a_copy: eqmdsk.AFile = a.copy()
    _ = (shot, flag, chisq, chord, header, footer, a_copy)


def check_kfile(k: eqmdsk.KFile) -> None:
    section: eqmdsk.NamelistBlock = k["IN1"]
    limitr: int = section["LIMITR"]
    value: FieldValue = section["BTOR"]
    k_copy: eqmdsk.KFile = k.copy()
    _ = (limitr, value, k_copy)


def check_sfile(s: eqmdsk.SFile) -> None:
    title: str = s["TITLE"]
    x: NDArray[np.float64] = s["X"]
    s.save(Path("s.output"))
    s_copy: eqmdsk.SFile = s.copy()
    _ = (title, x, s_copy)


def check_namelist(n: eqmdsk.Namelist, block: eqmdsk.NamelistBlock) -> None:
    n_copy: eqmdsk.Namelist = n.copy()
    block_copy: eqmdsk.NamelistBlock = block.copy()
    _ = (n_copy, block_copy)
