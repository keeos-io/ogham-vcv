# ogham-src — the firmware's own sources

**Do not edit anything in this directory.**

These are byte-identical copies of translation units from
[`keeos-io/ogham`](https://github.com/keeos-io/ogham), the firmware of the Ogham
Eurorack module. The plugin compiles them unmodified: the voices you hear in
Rack are not a reimplementation of the module's voices, they are the same code.

That claim only holds while these files are untouched, so it is enforced rather
than trusted. `tools/upstream_check.py` verifies every file here against a
sha256 recorded in `tools/upstream_manifest.json`, and CI runs it.

| To do this | Run |
|---|---|
| Check the copies are intact | `python tools/upstream_check.py` |
| See whether the firmware has moved | `python tools/upstream_check.py --fetch` |
| Take a newer firmware | `python tools/sync_upstream.py --ref v1.17` |

If a change to this code is genuinely wanted, it belongs upstream in the
firmware repository, where the hardware can be flashed with it and heard. A fix
that exists only here would be a silent fork of the module.

`ogham_main.cpp` is deliberately absent. It is a `main()` built on file-scope
globals and interrupt handlers, so it cannot be compiled into a Rack module; the
plugin re-creates it by hand as `src/OghamApp.cpp`. That file is watched rather
than copied — see `docs/firmware-differences.md`.
