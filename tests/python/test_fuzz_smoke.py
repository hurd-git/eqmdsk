from pathlib import Path

import pytest

import eqmdsk


def _e(value):
    return f"{value:16.9E}"


def _g_seed():
    rows = [f"{'FUZZ SEED':<48}{0:4d}{2:4d}{2:4d}"]
    rows.extend(
        "".join(_e(value) for value in values)
        for values in (
            (1, 2, 3, 4, 5),
            (6, 7, -0.5, 0.5, -2),
            (4, -0.5, 0, 6, 0),
            (7, 0, 0.5, 0, 0),
            (1, 2),
            (3, 4),
            (5, 6),
            (7, 8),
            (9, 10, 11, 12),
            (1, 2),
        )
    )
    rows.append(f"{0:5d}{0:5d}")
    return ("\n".join(rows) + "\n").encode("ascii")


def _a_seed():
    rows = [
        " 01-Jan-00 00/00/0000",
        "      1               1",
        " " + _e(1.0),
        (
            "*"
            + f"{1.0:8.3f}"
            + " " * 9
            + f"{1:5d}"
            + " " * 11
            + f"{0:5d} LIM {0:3d} {0:3d} QMF {0:5d}{0:5d}"
        ),
    ]
    rows.extend("".join(_e(record + offset) for offset in range(1, 5)) for record in range(17))
    rows.append(f" {0:5d}{0:5d}{0:5d}{0:5d}")
    return ("\n".join(rows) + "\n").encode("ascii")


def _mutations(seed):
    yield b""
    yield seed[:1]
    yield seed[: len(seed) // 2]
    yield seed + b"\0\xffTRAIL"
    step = max(1, len(seed) // 16)
    for position in range(0, len(seed), step):
        changed = bytearray(seed)
        changed[position] ^= 0xFF
        yield bytes(changed)
        yield seed[:position] + seed[position + 1 :]


@pytest.mark.parametrize(
    ("file_type", "seed"),
    [
        (eqmdsk.GFile, _g_seed()),
        (eqmdsk.AFile, _a_seed()),
        (eqmdsk.KFile, b"outside\0\n&IN\n A=1\n B=2*3.0D+00\n/\n"),
        (eqmdsk.SFile, b"x\ny\ntitle\n1 2 3 4\n"),
    ],
)
def test_deterministic_parser_mutation_smoke(tmp_path: Path, file_type, seed):
    source = tmp_path / "mutated.input"
    output = tmp_path / "mutated.output"
    for payload in _mutations(seed):
        source.write_bytes(payload)
        try:
            parsed = file_type(source)
        except eqmdsk.Error:
            continue

        try:
            parsed.write(output)
            file_type(output)
        except eqmdsk.Error:
            # A mutated input may remain parseable while violating the stricter
            # canonical write schema. It must still fail as a library error.
            pass
