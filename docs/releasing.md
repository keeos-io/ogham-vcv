# Cutting a release, and getting it into the VCV Library

Two separate things, and the second only needs doing once. A release is a tag and
four packages on GitHub. The Library is VCV's catalogue, which builds the plugin
itself from source and distributes it inside Rack.

## Cutting a release

### 1. Version and changelog

`plugin.json`'s `version` is `MAJOR.MINOR.REVISION` with no `v`, and **the major
tracks Rack's, not the plugin's maturity**. Rack's manifest documentation is
explicit: *"The MAJOR version should match the version of Rack your plugin is
built for, e.g. 2."* Their own example is `MyPlugin 2.4.2 would specify that your
plugin is compatible with Rack 2.X`.

So the first release was `2.0.0`, not `1.0.0`. A `1.x` plugin would read as
"built for Rack 1" — a different and incompatible generation — rather than "our
first release". The `.0.0` already says that.

Move the changelog's `## Unreleased` heading to `## X.Y.Z — YYYY-MM-DD`.

### 2. Tag

```bash
git tag -a v2.0.1 -m "..."
git push origin main
git push origin v2.0.1
```

The tag push starts CI, which runs the tests and builds `win-x64`, `lin-x64` and
`mac-arm64`, then drafts a release with those three attached.

### 3. The fourth platform

CI does not build `mac-x64`: its runner is `macos-13`, Intel hardware Apple no
longer sells and GitHub is winding down, and its queue ran past twenty minutes
while holding the whole run's logs. So, with the tag checked out and the tree
clean:

```bash
python tools/cross_build.py release
```

That builds `mac-x64` and uploads it. It refuses if HEAD is not at a tag or the
tree is dirty, because a package that is not the tagged source is worse than a
missing one — nothing downstream would ever notice.

### 4. Check and publish

```bash
gh release view v2.0.1          # four assets?
gh release edit v2.0.1 --draft=false
```

## Submitting to the Library, the first time

VCV **builds the plugin themselves** from a commit you name. You are not
uploading binaries; the packages above are only for people who install by hand.

### Before posting

The one check that actually matters is that a clean clone builds with their
toolchain, because that is exactly what they will do:

```bash
docker run --rm ogham-toolchain:all bash -c '
  cd /tmp && git clone -q --depth 1 --branch v2.0.1 \
      https://github.com/keeos-io/ogham-vcv.git fresh
  export PLUGIN_DIR=/tmp/fresh
  cd /home/build/rack-plugin-toolchain && make plugin-build-lin-x64 -j20'
```

Submodules are the usual reason this fails, which is why the firmware is vendored
into `ogham-src/` rather than referenced: a recursive clone of the firmware
descends into libDaisy and ST's CMSIS trees and dies outright.

Their stated requirements are only that the plugin is not malware and does not
violate intellectual property.

### Posting

One issue, at <https://github.com/VCVRack/library/issues>, with the title equal
to the **plugin slug** — `Keeos`. Not the module slug (`Ogham`), not a
description. Their words: *"Create exactly one thread in the Issue Tracker, with
a title equal to your plugin slug."*

The body wants the plugin name, licence, the URLs, and your email only if you
want it public:

```
Plugin: Keeos
License: MIT
Version: 2.0.0
Source: https://github.com/keeos-io/ogham-vcv
Commit: <the tagged commit>
Manual: https://github.com/keeos-io/ogham-vcv#readme
Author: Steven Collins — https://keeos.io

Ogham — a dual-voice bytebeat synthesizer, ported from the Keeos Ogham
Eurorack module and running the module's own DSP: seven of the firmware's
nine translation units are compiled into the plugin unmodified.

Panel components come from the Component Library (CC BY-NC); the panel
artwork is the module's own. See THIRD-PARTY.md.
```

A team member then reviews it, creates `manifests/Keeos.json`, and their farm
builds all four platforms — `mac-x64` included, whatever CI here does. Expect
days rather than hours.

## Updating a plugin already in the Library

Use the **same thread**, forever. One per plugin.

1. Bump `version` in `plugin.json` and cut a release as above.
2. Comment in the thread with the new version and commit hash.

## The bits that are easy to forget

- `mac-x64` is not built by CI. `cross_build.py release` is the whole reason that
  step exists; it was manual for exactly one release.
- The release is created as a **draft**. Publishing is a separate command.
- A tag cannot be moved once anyone has it. Cut a new revision instead.
