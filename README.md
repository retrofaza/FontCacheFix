# FontCacheFix

One-time tool that makes OWB reuse a pre-built fontconfig cache instead of
rescannning all fonts on the first run after installing AROS from an ISO.

## Why

On AROS, fontconfig decides that a font directory cache is still valid by
comparing the `checksum` field stored in the cache file header with the
modification time (in seconds) of the font directory itself. For the OWB that
ships in the AROS ISO (built against fontconfig 2.11.0) this is:

```
FcCacheTimeValid: cache->checksum == (int) dir_stat->st_mtime
```

(see `FcCacheTimeValid()` / `FcDirCacheValidateHelper()` in fontconfig 2.11.0's
`src/fccache.c`). The AROS fontconfig port has no `checksum_nano` field (that
was only added upstream in 2.13.1) and shortens the cache file name to the
first 8 characters of the MD5 of the font directory path:

```
PROGDIR:Conf/29a5abb3-x86_64-aros.cache-4     (29a5abb3 = first 8 of MD5("Fonts:TrueType"))
```

If a cache file is generated on the build machine but the ISO installer stamps
the font directory with the install time, the stored checksum no longer
matches, so fontconfig discards the cache and rescans every font on the first
OWB run (the splash screen indexation bar).

`fcachefix` rewrites the `checksum` field of an existing cache file to match
the current mtime of the font directory, so fontconfig accepts the pre-built
cache. It must be run **once**, right after installation, while the font
directory contents are known to match the cache.

The tool also understands the newer fontconfig 2.13.1+/2.16.0 cache format
(cache version 9, full 32-character MD5 names, `checksum_nano` field): for
those files it additionally zeroes `checksum_nano`, since the AROS stat layer
reports `st_mtim.tv_nsec == 0` always.

## Requirements

- The cache must have been built on AROS with the **same fontconfig version
  that OWB actually loads in the ISO**. A cache produced by fontconfig 2.16.0
  (cache-9) is rejected by fontconfig 2.11.0 and vice versa.
- The font directory path string must be identical to the one used when the
  cache was built (OWB AROS default: `Fonts:TrueType`), because the cache file
  name is derived from its MD5.
- The cache file must live in the directory listed in `<cachedir>` of the
  `fonts.conf` that OWB loads. On the current ISO stack that is
  `PROGDIR:Conf`; `fcachefix` also falls back to `PROGDIR:Conf/font`,
  `PROGDIR:fonts/cache` and `T:fonts/cache`.

## Build

Using the AROS cross compiler:

```sh
make CC=x86_64-aros-gcc       # or i386-aros-gcc
```

Or through the normal OWB CMake build (target `fcachefix`).

The tool is pure C and links nothing beyond the standard AROS POSIX layer.

## Usage

```
fcachefix [cachedir] [fontdir]
```

Defaults: `cachedir = PROGDIR:Conf`, `fontdir = Fonts:TrueType`.

The cache file layout is detected from the version suffix in the file name:
`.cache-9` and newer also get `checksum_nano` zeroed, older versions only get
`checksum` patched. The `checksum` field offset is derived with `offsetof()`
from the target ABI (48 on x86_64, 28 on i386).

Exit code `0` on success, `20` on bad arguments, `21` on errors (including "no
cache file found").

## Workflow for a new ISO

1. On an AROS system (e.g. a VM) with the final font set installed in
   `Fonts:TrueType`, run OWB once and let the font indexation finish. Use the
   OWB + fontconfig that the ISO will actually ship.
2. Copy the generated file from `PROGDIR:Conf/` (the `29a5abb3-*.cache-*`
   file) into the ISO so it lands in `PROGDIR:Conf/`.
3. Ship `fcachefix` and the `FontCacheFix` script, e.g. in `PROGDIR:Scripts/`
   (`make install` copies them to `Dist/Scripts/`).
4. After installing the ISO, run `FontCacheFix` once. The next OWB start loads
   the cache and skips the font scan.

## Verification

- Compare `stat Fonts:TrueType` (mtime, in seconds) with the `checksum` field
  in the cache file header (offset 48 on x86_64 / 28 on i386) — they must be
  equal.
- First OWB run after `FontCacheFix` should show no font indexation progress.
- Negative test: add a font to `Fonts:TrueType` — the directory mtime changes,
  the cache becomes invalid and OWB rescans again (the fixup is not repeated).

## Caveats

- Run the fixup only at install time, when the shipped fonts are in place.
  If OWB is updated/reinstalled with a different font set, run it again.
- The cache format must match the fontconfig that OWB loads. If the ISO stack
  is changed (e.g. to fontconfig 2.16.0), regenerate the pre-built cache with
  that fontconfig; `fcachefix` itself handles both formats.
