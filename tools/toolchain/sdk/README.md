# The macOS SDK goes here

Drop `MacOSX12.3.sdk.tar.xz` in this directory and the toolchain image gains the
two macOS targets. Without it the image builds `win-x64` and `lin-x64` and says
so.

The version matters: the toolchain pins `DARWIN_VERSION = 21.4`, which is
macOS 12.3, and a different SDK will not match the compiler triples it builds.

To produce it, on a Mac:

```bash
# Xcode 14.0.1 specifically, from developer.apple.com (a free Apple ID is enough)
xip --expand Xcode_14.0.1.xip

git clone https://github.com/tpoechtrager/osxcross.git
cd osxcross/tools
XCODEDIR=/path/to/Xcode.app ./gen_sdk_package.sh
```

That writes `MacOSX12.3.sdk.tar.xz` next to the script. Xcode is only unpacked,
never run, so it does not need to be a version that supports the host macOS.

**Never commit it.** It is Apple's, redistributing it is not permitted, and
`.gitignore` is set to keep it out. The same applies to any image built with it:
the SDK stays in the `COPY` layer even after the build deletes it, so an image
built from this directory must not be pushed anywhere public.
