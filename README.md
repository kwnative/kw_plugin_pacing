# kw_plugin_pacing

Licensed under **GPLv3**, with a game interoperability permission. See
[LICENSING.md](LICENSING.md) for scope, source-distribution obligations and
[third-party notices](THIRD_PARTY_NOTICES.md).

Universal native-wait Sleep(1) and timer policy for human and bot processes.

## Layout and build

Production implementation is under `src/`; supported/public or compatibility
headers are under `include/`. Generated files go only to ignored `build/`:
`build/bin` for DLLs/executables, `build/lib` for libraries, `build/obj` for
objects/dependency files, `build/config` for copied configuration templates.
Use GNU Make and x86 MinGW (`i686-w64-mingw32-gcc/g++/ar`).

```sh
make -j4
make sdk-paths
```

For relocated checkouts set `KWMOD_SDK_ROOT=/path/to/kw_modloader`.
It is the only mandatory SDK dependency: generic loader contracts and KW engine support
are both in its `include/` tree. Environment variables and Make arguments work.
`KWMOD_BIN_DIR` and `KWMOD_LIB_DIR` select matching SDK build artifacts;
`sdk-build` builds missing/stale SDK artifacts before linking. Compiler overrides
are `WINDOWS_CC`, `WINDOWS_CXX`, `WINDOWS_AR`; `BUILD_DIR` selects plugin output.

Run `make bundle` (or `make stage`) to assemble `build/bundle/` with this
plugin's DLLs, configuration and manifest in their engine-root-relative layout.
Override `BUNDLE_DIR=/path/to/staging` if needed. This is a plugin overlay,
not a standalone loader installation. Developer variants are not included.
Bundle generation replaces only previously staged, unmodified files and
refuses an unowned or locally edited output directory. Do not point it at an
installed game. See `kw_modloader/docs/BUNDLES.md` for composition and AI caveats.

Build and staging targets do not install into a game or overwrite user settings.
Use matching loader/SDK artifacts and preserve customized INIs when installing.
Runtime loading uses `.kwplugin` manifests; configuration uses `.ini` files.

## One pacing owner for every role

`kw_pacing` is the only native wait-patching implementation. It has no AI, FPS,
UI or threading dependency. The manifest uses `roles=any`. The common loader
activates the same plugin in the human game and each managed bot process;
AI contains neither a duplicate wait hook nor a private pacing service.
Only the validated stock zero-duration Sleep call is intercepted. Nonzero
arguments, simulation clocks, logic frequency and network protocols are unchanged.
Engine selectors are dynamically resolved through the loader SDK's shared
engine cache before mutation; there is no fixed-offset fallback or FPS-renderer
layout dependency. The six-byte call is reserved/written through the loader.

All settings are in `plugins/kw_pacing.ini`, beside this plugin:

| Key | Default | Meaning |
| --- | --- | --- |
| `enabled` | `1` | `0`: no hook, policy publication or timer request for this role. |
| `zero_sleep_delay_ms` | `1` | `1`: forward native Sleep(0) as Sleep(1); `0`: preserve Sleep(0). Nonzero waits are unchanged. |
| `timer1` | `1` | Request Windows 1 ms resolution, balanced on failure/process teardown; failure retains stock resolution. |
| `pacing_diagnostics` | `0` | Optional WAIT timing/CPU samples on the first actual calling thread. |

`[pacing]` supplies universal defaults. Optional `[pacing.rendered]` and
`[pacing.headless]` sections override individual keys. Missing override keys
inherit the common section. Values are startup-only integers `0` or `1`.
Managed bots select the loader's headless role even when their own renderer is
enabled; this avoids loading human-controller plugins recursively. For example:

```ini
[pacing]
enabled=1
zero_sleep_delay_ms=1
timer1=1
pacing_diagnostics=0

[pacing.rendered]
enabled=0
```

This example optimizes bots but leaves the human game untouched. Remove the
override to use the same optimization for both. Disable the manifest to omit
the plugin entirely. No AI-side switch needs changing in either case.

FPS automatically excludes a separately reserved wait through the generic
patch broker. Its optional pacing-policy contract adds semantic checks, not a
required runtime provider. Diagnostics go to the loader log: `kw_framework.log`
for the human and `kw_framework-bot-<pid>.log` for each managed bot.
