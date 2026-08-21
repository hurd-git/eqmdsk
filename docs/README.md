# Documentation

`eqmdsk` reads, edits, and writes complete EFIT files. It does not calculate
equilibrium properties, derive grids, plot data, or connect to external data
services.

Start with the page for the file you have:

- [G-file guide](gfile.md): GEQDSK equilibrium geometry, profiles, flux grid,
  boundary/limiter points, and COCOS conversion;
- [A-file guide](afile.md): scalar equilibrium summaries, diagnostic arrays,
  and optional records;
- [K-file guide](kfile.md): nested Fortran namelist mappings and canonical
  standard writing;
- [S-file guide](sfile.md): optional labels followed by four numeric columns.

The [Python API guide](python-api.md) explains the behavior shared by all four
classes: eager reads, explicit-path writes, field value types, writable NumPy
views, canonical output, and exceptions. [Formats and public
fields](formats.md) is the compact schema reference.

For stability and packaging details, see the [compatibility
contract](compatibility.md) and [release checklist](releasing.md).
