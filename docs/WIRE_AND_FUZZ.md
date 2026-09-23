# Transport faults and bounded protocol fuzzing

Evidence filenames and workspace-relative paths below refer to local validation records. See `../validation/README.md` for the published summary; raw local logs are not included.

`DevToolsChannel` owns a loopback discovery request and a native WebSocket
connection. A single reactor serializes socket reads/writes and pending-call
bookkeeping; callers may wait concurrently. Each request has a unique positive
ID and its own session. A response can complete only that pending call.

## Accepted messages and failure behavior

`decode_devtools_message` parses bounded JSON, rejects duplicate keys and then
validates the CDP envelope before looking up a pending request. Response IDs
must be positive JSON integers no larger than 2^53 - 1. Floating-point, boolean,
string, zero, negative and oversized IDs are refused. A response must contain
exactly one object result or structured error. Error codes must be integers in
the signed `int` range, with string messages. Events require a nonempty method
string and object parameters when supplied. Responses cannot also be events.
Unknown additional metadata is retained.

A supplied response `sessionId` must match the original request. Chrome can
omit it when rejecting an invalid session, so omission remains valid. Events
retain their session IDs. Valid late/unknown response IDs are ignored;
malformed messages are rejected even when their IDs are no longer pending.
Binary WebSocket messages are refused rather than interpreted as CDP text.

This fixes an actual reproduced defect: a response with ID `1.5` was converted
to integer `1` and satisfied request 1. The pre-fix failure and owned peer
receipt are retained in `../../evidence/UltimateBrowserJS/wire-fractional-before/`.
After strict decoding, the same input fails the call and closes the connection.

Ordinary structured CDP errors affect their request and leave the channel
usable. Timeout, cancellation and a throwing progress callback abandon the
pending ID; a later reply cannot satisfy the next call. A broken or invalid
transport fails all pending callers and marks the channel disconnected.
The channel is not silently reconnected or replayed: higher layers decide when
to reconnect, so browser mutations are not duplicated after uncertain failure.

Discovery and handshake share one connection deadline. The advertised endpoint
must use `ws://127.0.0.1` or `ws://localhost` on that same discovered port.
Paths must be ASCII request targets without controls, spaces or fragments;
percent-encoded path/query bytes are permitted. Redirects, remote hosts,
alternate ports and TLS endpoints are not followed. Malformed discovery
descriptors fail before any WebSocket request is sent.

## Bounds

| Resource | Limit |
|---|---:|
| Discovery response body | 1 MiB |
| Advertised WebSocket endpoint | 8192 bytes |
| Outgoing JSON request | 16 MiB |
| Incoming WebSocket text message | 32 MiB |
| Parsed JSON depth / structural events | 64 / 1,000,000 |
| Buffered events | 10,000 and 8 MiB combined payload |
| Pending calls / queued writes | 1024 each |
| Queued outgoing payload | 64 MiB |
| Explicit connection / call timeout | 1–60,000 ms |

Exceeding the event buffer closes the channel instead of dropping events
silently. Outgoing request-size refusal occurs before transmission and leaves
the connection usable. Count/byte limits on queued requests prevent unbounded
growth, but the current fault matrix does not force saturation of all 1024
pending callers or the 64 MiB outgoing queue. Do not interpret message/event
limit tests as that distinct stress result.

## Real socket fault matrix

`tests/integration/wire_fixture.py` is a standard-library Python HTTP/WebSocket
peer bound to an ephemeral loopback port. It launches the actual native
`relay-wire-fault-tests` executable, records received masked frames and messages,
and closes and joins its owned server threads after each scenario. It does not
start Chrome or inspect user browser state.

The matrix contains **56 scenarios / 169 native checks**:

- Discovery status/JSON/duplicate fields/missing or wrong endpoint, wrong host,
  port or scheme, control/NUL/fragment path, oversized/truncated body, and delay.
- Handshake refusal, incorrect acceptance key, delay, and discovery plus
  handshake jointly exhausting one deadline.
- Fragmented HTTP, UTF-8 scalars split across WebSocket continuation frames,
  interleaved ping/pong, session events and exact event consumption.
- Eight simultaneous callers, reversed responses and two distinct sessions;
  valid unknown responses; disconnect with all eight callers pending.
- Structured protocol errors and following recovery; timeout, cancellation and
  progress-callback failure followed by an actual late reply and a fresh call.
- Outgoing request-size refusal, peer close/EOF, incoming WebSocket-size header
  rejection, and event count/byte overflow.
- Binary/invalid/duplicate JSON and malformed IDs, response/error/event shapes,
  session type and mismatched session, including empty/array/null envelopes.

The fixed peer scenarios complement the separate real-Chrome wire and browser
tests. They are failure injection, not proof of Chrome page behavior. Current
build/run receipts and complete regression totals are in [CHECKPOINT.md](CHECKPOINT.md).

```sh
cmake --preset debug
cmake --build --preset debug
python3 tests/integration/wire_fixture.py \
  --binary build/debug/relay-wire-fault-tests \
  --evidence ../evidence/UltimateBrowserJS/wire-debug-accepted-final
```

## Bounded fuzz run

`relay-protocol-fuzz` compiles the production JSON parser, CDP decoder, action
catalog/argument normalization, MCP endpoint and line framing directly with
LLVM libFuzzer, ASan and UBSan. Its handler returns normalized data; it never
executes browser actions. Inputs exercise accepted/rejected envelopes,
initialization/call sequences, aliases, numeric values, nested arguments and
arbitrary byte chunk boundaries. Fragmented and unfragmented framing must yield
the same lines or the same size refusal. Accepted CDP messages and MCP replies
must survive JSON round trips without changing value.

The first bounded run used seed **9814**, maximum input **65,536 bytes**, a
3-second per-input limit and 2048 MiB RSS cap. It completed **59,083 executions
in 31 seconds**, with no crash/ASan/UBSan report; recorded peak RSS was 549 MiB.
This is finite evidence, not exhaustive parser or protocol proof. It does not
fuzz Chrome, DOM actions, OS socket implementations or concurrent queue state.

```sh
cmake -S . -B build/fuzz -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++ \
  -DBUILD_TESTING=OFF -DCHROMERELAY_BUILD_FUZZER=ON
cmake --build build/fuzz --target relay-protocol-fuzz -j4
mkdir -p ../.work/chromerelay-protocol-artifacts
python3 tests/fuzz/seed_corpus.py ../.work/chromerelay-protocol-corpus
build/fuzz/relay-protocol-fuzz ../.work/chromerelay-protocol-corpus \
  -max_total_time=30 -timeout=3 -rss_limit_mb=2048 -max_len=65536 \
  -seed=9814 -print_final_stats=1 \
  -artifact_prefix=../.work/chromerelay-protocol-artifacts/
```

Use a Clang installation that includes libFuzzer; the shown path is the tested
macOS LLVM installation. The fuzz option defaults off and does not instrument
or change the normal native library/executable. Mutated corpus and fault
artifacts live outside source, with a manifest and run log under the evidence
directory. Reusing the evolved corpus is a different run from the original
seed-only start; execution counts and timing will vary.

The final 1.0.0 source was rebuilt and fuzzed from a fresh seed corpus with seed
9819 and the same bounds: **67,587 executions / 31 seconds**, peak RSS 562 MiB,
no crash artifact or sanitizer report. `delivery-fuzz-*` and
`build-delivery-fuzz.txt` contain the final receipts. The earlier 59,083-execution
run remains historical evidence with its separate corpus.
