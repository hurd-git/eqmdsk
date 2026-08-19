# Public compatibility corpus

The normal test suite is self-contained and never accesses the network. This
directory records optional, redistributable compatibility inputs that can be
downloaded explicitly with `fetch.py` into a build directory:

```console
python tests/public_data/fetch.py --output build/public-data
EQMDSK_PUBLIC_FIXTURE_DIR=build/public-data python -m pytest \
  tests/python/test_public_fixtures.py
```

Both inputs come from the MIT-licensed
[FreeQDSK repository](https://github.com/freegs-plasma/FreeQDSK). They are
fetched from immutable Git commits and verified before use. No downloaded data
are added to an eqmdsk source distribution or wheel.

| Local name | Upstream file and commit | SHA-256 |
| --- | --- | --- |
| `freeqdsk-test-1.aeqdsk` | [`tests/data/aeqdsk/test_1.aeqdsk` at `42ec75c`](https://github.com/freegs-plasma/FreeQDSK/blob/42ec75c35e0e9a04eeb68efe3e5f0a40948db809/tests/data/aeqdsk/test_1.aeqdsk) | `05998d7483dd75dfa627c7bb884aa0442a2b253a6f150fda70db504cf5205f4b` |
| `freeqdsk-test-1.geqdsk` | [`tests/data/geqdsk/test_1.geqdsk` at `55a92a3`](https://github.com/freegs-plasma/FreeQDSK/blob/55a92a3091a8e1bfc4a6288cf8d5c27916b717cc/tests/data/geqdsk/test_1.geqdsk) | `f2ac2c1680221c6b1a3f847a573ecdbeed2101b54286946b527e11cc4c1b1b98` |

The files are used without modification. See the
[upstream MIT license](https://github.com/freegs-plasma/FreeQDSK/blob/main/LICENSE).
