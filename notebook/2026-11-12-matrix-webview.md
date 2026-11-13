# Matrix WebView provider boundary — 2026-11-12

## Status and question

The archived Ny WebView project has been left untouched. Its modern replacement
is being built as `exec-nil-framework-webview`, with Matrix-facing library code
and separate WebView2, GTK4/WebKitGTK 6 and AppKit/WKWebView providers. Actual
browser qualification remains separate from the framework bridge tests below.

The question is how a Matrix application can observe a browser's asynchronous
work consistently through interpreted NIL and LLVM AOT/JIT, without reviving the
old unchecked generic interop layer.

## Decision

The versioned `webview.v1.*` intrinsic family uses the standard nominal
`Result<number,text>` and `Result<text,text>` schemas. The frontend, loaded-NIL
interpreter and native backend each validate the declarations. A provider is
selected by an explicit absolute path to a trusted native library. The C ABI
contains fixed-width fields and borrowed text views; no toolkit object, callback
into Matrix, allocator or C++ container crosses it. Loaded modules remain pinned
for process lifetime because engine callbacks may outlive a closed window.

Each execution owns its context on the UI thread. Runtime window and event
handles are process-unique and checked against that context. A poll copies all
event fields before releasing the provider's snapshot, including on malformed
output. Closing a window invalidates its retained runtime snapshots. Independent
contexts remain unaffected. Neither browser state nor these external handles is
part of the interpreter's pure same-process checkpoint profile.

Creation yields a pending window and an eventual Ready or InitializationFailed
event. Navigation and synchronous function evaluation yield operation identities
with explicit completion or failure. Only one such operation is pending per
window; deadlines make a timed-out window unusable until closed and recreated.
Cancelling a request does not claim that executing JavaScript was forcibly stopped.

The initial common navigation profile authorizes one URL request. Redirects and
page-initiated navigation are reported for explicit application approval instead
of being followed automatically. This avoids making different promises on macOS
15, whose public navigation-action API does not identify HTTP redirects the way
the other engines do. Popups, downloads and native host commands are outside this
profile. The migration document tracks legacy features separately rather than
advertising unimplemented cross-platform aliases.

## Message and JSON identity

A message's sender metadata must come from the native engine or remain Unknown.
WebKitGTK 6's public message callback does not expose authenticated sender frame
metadata. A JavaScript field or the browser's current URL cannot fill that gap.
Capability bits distinguish browser-reported URLs, security origins and frames.
An event cannot claim metadata that its provider did not advertise.

Script results retain JSON.stringify's serialized text, with distinct Undefined,
script exception, serialization failure and unsupported Promise/thenable cases.
An independent review caught a real boundary mismatch: the framework's strict
configuration JSON parser rejected escaped lone surrogates and nesting levels
accepted by the provider. Both sides now use byte-identical WebView JSON grammar
headers: bounded UTF-8 JSON text, depth 64, no decoding of escaped UTF-16 units.
This preserves a legitimate `JSON.stringify` result without turning it into an
invalid UTF-8 scalar string. Depth 65 is rejected explicitly.

For future AI tooling, operation identity and copied events provide an observable
boundary for browser actions. They do not grant page messages authority to invoke
host operations, and they do not automatically log page content or credentials.

## Evidence and next qualification

- 57 framework host checks cover provider ABI rejection, context/thread and
  handle ownership, event release, provenance combinations and JSON boundaries.
- 28 intrinsic contract checks cover malformed declarations, disabled source
  checking, typed failures and execution routes; the existing 40-check UI
  intrinsic contract also passes after sharing native lowering code.
- The Matrix consumer contract passed text NIL, binary NIL, LLVM AOT and JIT
  using a synthetic provider, including records retained after event/window
  teardown. This is interface proof, not browser proof.
- The initial shared provider logic passed 196 native controls with sanitizers
  and 36 checks of the generated JavaScript transport. Browser qualification
  was a separate follow-up to this initial synthetic-provider evidence.

### Browser qualification follow-up — 2026-11-13

The public [WebView qualification PR](https://github.com/exec-software/exec-nil-framework-webview/pull/1)
now passes on Linux, Windows, Intel macOS and Apple Silicon macOS at
`63d3e49d953179c81231402cf07dc7d6dbe17939`, using framework
`e7bc2a00e6854084b7b5084e29fa50456cbe8977`. Each host runs all 17 browser
behavior groups through text NIL, binary NIL, AOT and JIT, alongside four
synthetic-provider routes, 209 shared controls and 36 JavaScript checks.
The [platform notes](https://github.com/exec-software/exec-nil-framework-webview/blob/63d3e49d953179c81231402cf07dc7d6dbe17939/docs/platform-support.md)
describe the engine-version evidence, request counts, cleanup checks, source
seals and limits of the implemented profile. This qualifies the initial API profile;
it does not claim that every archived WebView feature has been ported.

No accepted SDK, installer or release deployment is changed by this work.
