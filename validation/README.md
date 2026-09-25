# Local validation record

The 2026-09-25 review passed **1,533 checks** in each fresh Debug, Release and
ASan/UBSan build on macOS arm64: 155 contracts, 1,146 browser/MCP checks and
232 socket faults. Forty-six new checks cover action target/session affinity, cookie-context
isolation and asynchronous file selection. Installed CLI (17 checks) and independent consumer (seven) passed.
See [the current verification record](../docs/CHECKPOINT.md).

ChromeRelay 1.0.0 was initially validated locally on macOS arm64 on 2026-09-23.
Debug, Release and ASan/UBSan each passed **1487 checks**. The final bounded
fuzz run completed **67,587 executions in 31 seconds** without a crash or
sanitizer report. Finite tests are not an arbitrary-input equivalence proof.

The repository contains the source and reproducible tests. Full raw local logs,
build directories and private workspace material are not published. Evidence
filenames in the detailed validation documents identify the original local
records, not files promised in this repository. Selected transcripts for the current
review are published below; full raw local evidence remains outside the repository.
Linux and Windows execution remain unverified.

`local-source-manifest.json` preserves the accepted local delivery's file hashes.
The initial publication edited only documentation to remove workspace-specific handoff text
and explain evidence availability. Program code, fixtures, build configuration,
tests and licenses initially retained their accepted bytes. Subsequent Git commits
record later fixes; this manifest is historical and does not describe the current checkout. The initial Git commit records
publication; it does not manufacture a prior development history.

The functional baseline is `UltimateBrowserJS`. Its exact commit and retained attribution
are documented in the project notices. That historical repository may be private;
licenses and source provenance remain available here.

Each historical 2026-09-23 full suite included 155 contracts, 1,100 real Chrome/MCP checks and 232 socket
fault checks. Historical selected TSan coverage: 549 checks. All 75 legacy names were called.
Installed CLI: 17 checks; independent consumer: seven. Extracted binary validation
repeated 17 CLI, 116 MCP compatibility and seven consumer checks. Owned temporary
Chrome 153 profiles were cleaned up. See `docs/CHECKPOINT.md`.

## Current review evidence

- [Machine-readable review result](current-review.json) and [sanitized test transcripts](review-2026-09-25/).
- [CMake 3.24 preset compatibility result](current-review-build.json).
- [GitHub macOS verification](https://github.com/dhtfish988/ChromeRelay/actions/workflows/verify.yml) builds Release, runs native and owned-browser tests, installs the package and exercises an independent consumer. Read the result for the exact commit; a workflow file alone is not a successful run.

The original 1.0.0 archives remain historical artifacts. Use the current Git commit
for these fixes. This review did not repeat the historical TSan and fuzz runs.

The first hosted run exposed missing C++20 cancellation support in the macOS 15
default Xcode 16.4 library/SDK. Configuration now checks the actual compile/link
capability before the main build. CI explicitly uses Xcode 26.6 on macOS 26; see
the build review above for the failed run and exact validation scope.

Final local Release verification used Chrome for Testing 152.0.7977.82; Debug
and ASan/UBSan used Chrome 153.0.8010.54. The hosted Chrome 152 failure led to
an explicit default-context mapping fix; independent probes on both versions
also exposed asynchronous directory completion. The final full transcripts above
include both fixes. A transient local CLI startup timeout and successful unchanged
follow-up checks are recorded in the machine-readable result.

A later hosted run failed the highlight-restoration assertion after a fixed client
sleep. The final follow-up changes only service-test synchronization and related
documentation: it awaits real lease completion and checks overlap cleanup with
controlled callback ordering. The revised 55 service checks passed in Release
with Chrome 152 and 153, and Debug/ASan with Chrome 153. The full local matrices
above precede this test-only follow-up; their production code is unchanged.
The original hosted scheduling cause remains unproven. Focused transcripts and
the diagnostic scope are recorded in `current-review.json`.
