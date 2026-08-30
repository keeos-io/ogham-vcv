# Submitting Ogham to the VCV Library

Notes gathered 2026-08-30 from the primary sources and from reading the last
~50 submissions in the library issue tracker. VCV has no single "submission
guidelines" page: the written rules are spread over four documents, and the
checks that actually gate a submission are not documented anywhere -- they are
only visible in the failure reports the integration bot files against
submitters' repos.

Sources:

- <https://github.com/VCVRack/library> (branch `v2`) -- the submission process
- <https://github.com/VCVRack/rack-plugin-toolchain> (branch `v2`) -- the public
  build and analysis commands
- <https://github.com/cschol/rack-library-tools> -- the manifest validator
- <https://vcvrack.com/manual/PluginDevelopmentTutorial> -- release steps
- <https://vcvrack.com/manual/Manifest> -- `plugin.json` fields
- <https://vcvrack.com/manual/PluginLicensing> -- ethics guidelines, licensing
- Failure reports filed by `rack-integration-tools` on submitters' own repos,
  which quote the exact commands run

## What actually gates a submission

Submissions are checked by an automated tool that identifies itself with an
HTML marker, `<!-- rack-integration-tools:failure -->`, in a comment posted by
@cschol in the plugin's library thread. That comment links to an issue the tool
files on the submitter's own repository, containing the findings and the
commands to reproduce them.

Across recent submissions the failure classes are:

| Class | Frequency | Example |
|---|---|---|
| Static analysis issues | dominant | most rejections |
| Manifest validation | occasional | invalid module tags, invalid URL |
| Build failure (per platform) | occasional | mac-x64 SDK, lin-x64 header |
| Integration failure | rare | repo / submodule problems |

The public toolchain's `make plugin-analyze` is **not** the gate. That target
runs cppcheck alone, at default severity. The integration tool runs three
separate analyses, and its cppcheck settings differ from the toolchain's.

### 1. cppcheck

Reproduce line quoted in the tool's own reports:

```
cppcheck <explicit list of *.cpp and *.hpp under the plugin's source dirs> \
    --std=c++11 --max-configs=1 --enable=warning -j 8 -q --xml
```

- `--enable=warning`, so warning severity counts -- stricter than the public
  toolchain, which enables nothing beyond errors.
- **No `--inline-suppr`.** Inline `// cppcheck-suppress` comments in our source
  are ignored on their side. We have one, at `ogham-src/formulas.h:45`
  (`shiftNegativeLHS`); that id is *portability* severity, which their
  `--enable=warning` does not turn on, so it is latent rather than live. It
  becomes a failure the day they widen the severity set, and it cannot simply
  be edited away here: `ogham-src/` is hash-verified against the firmware by
  `tools/upstream_check.py`, so the fix would have to land upstream in the
  firmware first.
- The file list is explicit and covers the repo's source directories, not just
  `src/`. Directories named `vendored` or `thirdparty` are excluded
  automatically -- cschol confirmed this on keeos-io/ogham-vcv#1, which is why
  DaisySP was moved to `thirdparty/`. `ogham-src/` is our own firmware, not
  third party, so it stays in scope and has been analysed.
- Headers are reported on even when reached only via an including `.cpp`.

### 2. Pattern checks

A VCV-specific linter with no public implementation. Rules observed in reports
between June and August 2026:

- `[error] [manifestModelMissing]` -- a `plugin.json` module slug with no
  matching `createModel()` call in source. Rack refuses to load the *entire*
  plugin, not just that module.
- `[warning] [committedBinary]` -- a compiled build artifact committed to the
  repo (e.g. `plugin.dll`).
- `[warning] [fontImageCachedOutsideDraw]` -- `loadFont`/`loadImage` cached
  outside a safe draw callback. See
  <https://vcvrack.com/manual/Migrate2#2-Potential-runtime-bugs>.
- `[style] [nullModuleDrawBailout]` -- `draw()` returns immediately when
  `module` is null. The module browser and the library website preview render
  with `module == nullptr`, so such a widget appears blank in both.

Ogham is clean on all four as of 89842f2: one manifest module (`Ogham`)
matching `createModel<Ogham, OghamWidget>("Ogham")` at `src/Ogham.cpp:700`; no
tracked binaries; no `loadFont`/`loadImage` anywhere; and all three `draw()`
overrides in `src/widgets.hpp` render unconditionally.

### 3. clang-tidy

Reproduce line quoted in the tool's reports:

```
run-clang-tidy -clang-tidy-binary=clang-tidy -p . \
    -header-filter=^src/ -checks=-*,clang-analyzer-* -j 8 <source dirs>
```

preceded by `bear -- make RACK_DIR=<path to your local Rack-SDK>`.

This is the check that found `clang-analyzer-security.ArrayBound` in
`src/Ogham.cpp` on our first submission -- cppcheck missed it. Note that
`run-clang-tidy` is given *directories*, so it analyses every translation unit
under them that appears in `compile_commands.json`.

## Where our CI differs from theirs

Verified against 89842f2. Nothing here is currently failing; these are coverage
gaps that could let a finding through to submission.

The gaps found on 89842f2, and what was done about each:

| | VCV | Ours, before | Change |
|---|---|---|---|
| cppcheck scope | all source dirs | `src/` only | scans `src/ ogham-src/` |
| cppcheck version | 2.16.0, built from source | Ubuntu's 2.13 | builds and caches 2.16.0 |
| cppcheck severities | `warning` | `warning,performance,portability`, exhaustive | unchanged, already a superset |
| cppcheck suppressions | none honoured | `--inline-suppr` | kept -- see the firmware note above |
| clang-tidy inputs | source dirs, all TUs | `src/*.cpp src/shim/*.cpp` | adds `ogham-src/*.cpp` |
| clang-tidy header filter | source dirs | `^(src\|src/shim)/` | `(^\|/)(src\|ogham-src)/` |
| manifest validation | `rack-manifest-validator.py` | not run | runs it from their `main` |

Nothing was failing when these were widened: VCV's exact cppcheck invocation
over `src` + `ogham-src` with cppcheck 2.16.0 reports nothing, and the manifest
validator passes. The point is coverage, so that the next finding surfaces in
our CI rather than in a rejection.

The one check we still cannot run locally is the pattern linter, which has no
public implementation. Its four known rules have to be audited by eye -- see
the list above.

## Submission checklist, in order

1. **Ethics and safety gate.** Follow the VCV Plugin Ethics Guidelines -- no
   cloning the brand name, model name, logo, panel design, or component layout
   of an existing hardware or software product without permission -- and do not
   harm the user's computer or privacy.
2. **Licensing.** Ship `LICENSE.txt`. Plugins built against the GPLv3+ Rack SDK
   are GPLv3 derivative works unless covered by VCV's Non-Commercial Plugin
   License Exception. Component Library panel graphics are non-commercial-only
   without a commercial licence.
3. **Manifest correctness.** `plugin.json` requires `slug`, `name`, `version`,
   `license` (SPDX identifier), `author`, and per-module `slug` + `name`. Slugs
   are permanent. `version` is `MAJOR.MINOR.REVISION` with the major matching
   Rack, so `2.x`. Every module slug needs a matching `createModel()` call.
   Tags must match the predefined strings exactly. Run the manifest validator.
4. **Build clean against the plain SDK**, then `make dist`. Smoke test with
   `make install`, find the module in the browser, check `log.txt` for load
   warnings.
5. **Cross-compile all four targets** -- lin-x64, win-x64, mac-x64, mac-arm64.
   This is where submissions fail once static analysis is clean. See
   `docs/cross-compiling.md`.
6. **Run all three analyses** above and get them to zero.
7. **Repo hygiene.** The library tracks each plugin repo as a git submodule
   under `repos/` (`.gitmodules` in `VCVRack/library@v2`), so the repo must be
   publicly cloneable and any submodules of our own must be public and pinned.
   No committed build artifacts.
8. **Open exactly one issue** at <https://github.com/VCVRack/library/issues>
   with the title equal to the plugin **slug**. Ours is
   <https://github.com/VCVRack/library/issues/955>. That thread is the
   permanent channel.
9. **After fixing a reported failure, comment in the library thread** with the
   version and the full commit hash. This is the step that re-queues the
   submission; replying only in the downstream issue the bot filed does not.
   The bot's issue on our repo should be closed once the findings are fixed.
10. **For later updates:** bump `"version"` in `plugin.json`, push, then comment
    in the thread with the new version and the full commit hash
    (`git rev-parse HEAD`). The README is emphatic: "Please do not just give
    the name of a branch like `master`." A maintainer closes the thread when
    the build lands and reopens it for the next update.

Closed-source or commercial plugins skip the issue tracker and email
contact@vcvrack.com instead.
