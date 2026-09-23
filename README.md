# ChromeRelay

**ChromeRelay 1.0.0** is a C++20 MCP-to-Chrome DevTools bridge. It connects to an
existing loopback Chrome and provides 54 canonical browser actions or 75 legacy
UltimateBrowserJS names. Native transport, sessions, navigation, inputs, files,
workflows and cancellation run without a Node or Playwright runtime. Small
embedded JavaScript adapters handle DOM access and browser-side value conversion.

Start with [GETTING_STARTED.md](docs/GETTING_STARTED.md) for installation, a
separate Chrome profile, MCP host configuration and the C++ library example.

```sh
cmake --preset release
cmake --build --preset release
ctest --preset release --verbose
cmake --install build/release --prefix "$PWD/dist/ChromeRelay-macos-arm64"
dist/ChromeRelay-macos-arm64/bin/chrome-relay --port 9222
```

Build dependencies: C++20, CMake, Ninja, Boost headers and nlohmann/json. Exact
validated versions are in `dependencies.lock.json`. The macOS executable uses
system dynamic libraries. Build and install from this source tree. A [validation summary](validation/README.md)
records the locally accepted version and its verification boundaries.

Default names describe their domain: `page_navigate`, `element_click`,
`element_type`, `frame_enter`, `page_capture`, `workflow_steps`. Use `--catalog`
to inspect schemas or `--compat-tools` to advertise the original 75 names. The
server exits by disconnecting; it does not close your browser. Repeated
`--allow-root DIR` flags replace default home/temp upload and screenshot roots.

- [COMPATIBILITY.md](docs/COMPATIBILITY.md): all names, legacy result formats,
  preserved contracts and intentional differences.
- [CLICKS.md](docs/CLICKS.md), [KEYBOARD.md](docs/KEYBOARD.md),
  [FRAMES.md](docs/FRAMES.md): native input, targeting and frame scope.
- [NAVIGATION.md](docs/NAVIGATION.md), [PAGE_SERVICES.md](docs/PAGE_SERVICES.md):
  navigation, recovery, dialogs, cookies, storage and accessibility.
- [FILES_AND_CAPTURE.md](docs/FILES_AND_CAPTURE.md),
  [WORKFLOWS.md](docs/WORKFLOWS.md), [CANCELLATION.md](docs/CANCELLATION.md):
  file limits, composition, deadlines and side effects.
- [CHECKPOINT.md](docs/CHECKPOINT.md): measured acceptance and exact boundaries.

Final Debug, Release and ASan/UBSan each passed **1,487 checks**: 155 contracts,
1,100 actual Chrome/MCP checks and 232 socket/keyboard fault checks. Selected
ThreadSanitizer coverage passed **549 checks**. The installed CLI passed 17 checks;
an independently built installed-library consumer passed seven browser checks.
A finite protocol fuzz run completed **67,587 executions in 31 seconds** without
a crash or sanitizer report. All browser tests used owned temporary Chrome
profiles and local fixtures, with cleanup receipts. The Release MCP suite used
the installed executable.

Validated platform: macOS arm64, Chrome 153. Linux/Windows have not been run;
Windows needs adaptation of POSIX file/stdin handling. Arbitrary page layouts,
extended Playwright selectors and every possible parameter combination are not
claimed equivalent. Drag is within one selected document; snapshots explicitly
use native `ax-yaml`. See the linked guides before migrating a caller.

Functional baseline: UltimateBrowserJS commit
`133797d29cedf19b40f5cdcdcf4aac80a9b4941d` (MIT). That project's 24 isolated tests
used a stub browser and are historical evidence only. ChromeRelay connects to a
Chrome debug port and can run page script and read or change cookies and storage.
The checks recorded here used temporary profiles on macOS. They do not cover
arbitrary pages. Old-server probes supply 29 evaluation, 30 keyboard, 15
input-target, 28 click and 16 CSS vectors for the rewrite.

New implementation copyright 2026 dhtfish98, MIT. Original attribution and
all dependency license texts are retained in `LICENSE`, `THIRD_PARTY_NOTICES.md`
and `licenses/`. Embedded Playwright keyboard mapping data retains Apache-2.0
attribution; it is not newly authored mapping data or a runtime dependency.

Local validation and publication scope: [validation/README.md](validation/README.md).
