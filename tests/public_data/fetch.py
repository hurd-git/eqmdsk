"""Fetch the pinned, optional public compatibility corpus."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
from urllib.request import Request, urlopen


FIXTURES = {
    "freeqdsk-test-1.aeqdsk": (
        "https://raw.githubusercontent.com/freegs-plasma/FreeQDSK/"
        "42ec75c35e0e9a04eeb68efe3e5f0a40948db809/"
        "tests/data/aeqdsk/test_1.aeqdsk",
        "05998d7483dd75dfa627c7bb884aa0442a2b253a6f150fda70db504cf5205f4b",
    ),
    "freeqdsk-test-1.geqdsk": (
        "https://raw.githubusercontent.com/freegs-plasma/FreeQDSK/"
        "55a92a3091a8e1bfc4a6288cf8d5c27916b717cc/"
        "tests/data/geqdsk/test_1.geqdsk",
        "f2ac2c1680221c6b1a3f847a573ecdbeed2101b54286946b527e11cc4c1b1b98",
    ),
}


def fetch(output: Path) -> None:
    output.mkdir(parents=True, exist_ok=True)
    for name, (url, expected) in FIXTURES.items():
        request = Request(url, headers={"User-Agent": "eqmdsk-compatibility-test/0.9"})
        with urlopen(request, timeout=30) as response:
            data = response.read()
        actual = hashlib.sha256(data).hexdigest()
        if actual != expected:
            raise RuntimeError(
                f"checksum mismatch for {name}: expected {expected}, got {actual}"
            )
        (output / name).write_bytes(data)
        print(f"verified {name}: {len(data)} bytes, sha256={actual}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    arguments = parser.parse_args()
    fetch(arguments.output)


if __name__ == "__main__":
    main()
