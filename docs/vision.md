# Scuzz Lang vision

Long-term planning arcs: open work and risks. Product intent, design locks, and language direction: [`philosophy.md`](philosophy.md). Keep/cut: [`compatibility.md`](compatibility.md). Ranked gaps: [`gaps.md`](gaps.md). Later tune work: [`optimization.md`](optimization.md).

Edit this file when the next-step order changes.

## Open work

Next: make the language usable for general application development. Prioritize compiler correctness, memory ownership, type composition, standard kits, and tooling. Use examples to prove these capabilities through the built-in verification strategy. Specific application workflows do not define the scope.

### Success bars

**v0** — Install CLI (`curl …/install.sh | sh`, or checkout `./scripts/install.sh`) → `scuzz new` (IO) or `scuzz new --ui` (Counter as `View` + builtin `IO`) → `scuzz fuzz --iterations 0`, and `scuzz run` (`--headless` for UI). Desktop when available. Language `Resource` / `Stream` / `Net.serve` ship (`examples/io`).

**v1** — Shipped `scuzz` is the Scuzz CLI (GitHub Releases; `package_release.sh` / `install.sh`). Cut a release with the GitHub `release` workflow. Kernel surface is proven by examples. `fuzz` lives on that CLI.

### Current arcs

The webhook receiver authenticates GitHub HMAC-SHA-256 signatures before JSON parsing. It stores the latest received JSON object after authentication. HTTP 200 follows a complete file replacement. A failed write returns 503. Invalid signatures and requests preserve the prior file. Its scenarios send concurrent valid and invalid deliveries through the persistent server. Claims require a complete authenticated payload after the batch. A final delivery checks that the server continues to accept requests. Network and filesystem faults can preserve the prior payload or a complete accepted payload. It uses HTTP behind an HTTPS reverse proxy. It has no delivery queue or deduplication.

The API report fetches authenticated JSON records and writes an open-record report. It follows Link headers with the next relation. `Net.nextLink(currentUrl, header)` resolves the next link without effects. Relative links resolve against the current page. Next links must keep the scheme, host, and port. Link headers take priority over numeric X-Next-Page headers. A Link header without a next relation ends the report. Without Link, it follows numeric X-Next-Page headers on the configured URL. Numeric page numbers must increase and cannot exceed 100. A numeric next-page URL replaces existing page query fields, including percent-encoded names. It keeps other query values and excludes the fragment. Link pagination rejects repeated URLs and stops before page 101. It writes only after all pages succeed. It retries HTTP 429, 502, 503, and 504 at most twice per page. Retry-After accepts seconds or an HTTP date. `Net.retryAfterMillis(value, nowMs)` parses the delay without reading a clock. It accepts IMF-fixdate, RFC 850, and asctime dates. Expired dates yield zero. Invalid values and delay overflow yield -1. The report reads the current time through `Clock.realTime`. Without that header, HTTP 429 waits 1000 ms and gateway errors wait 100 ms. Invalid or ambiguous Retry-After values stop the request. One timeout covers all pages, attempts, and delays. Its corpus covers cursor links, cross-origin links, page cycles, the page limit, pagination, invalid page progression, recovery, retry exhaustion, rejected credentials, invalid JSON, failed status, and timeout. File claims check report contents. State comparisons verify that failed requests preserve arbitrary prior report text. Under filesystem faults, report claims require the prior contents or a complete new report.

The network UI fetches JSON through the shared Net API. It shows loading, failure, and success. Input continues during a request. Retry preserves the tap count. Native UI loops yield to IO fibers. Session exit cancels IO tap handlers. iOS and macOS GUI requests use URLSession with platform certificate trust. CLI and server requests keep the OpenSSL transport. Simulation uses the shared hermetic dispatch. Capture-safe host reload and physical iPhone proof remain open.

Ranked list: [`gaps.md`](gaps.md).

## Risks

| Risk | Mitigation |
| --- | --- |
| Language + UI + tooling is huge | Small subset. Vertical slices. Counter before generality |
| Dialect unexercised by apps | Kernel examples. `check` / `fuzz` on passing `examples/`. `examples/bad-*` are expected-fail |
| Effects too weak or too heavy | Builtin IO. Pure `View`. `Ui` at session boundary |
| Hidden nondeterminism | Closed impurity + hermetic TestRuntime. No live sockets under sim |
| Properties become brittle dump goldens | Named observations. Mutation kills weak oracles. Live-graph paint is not a dump fixture |
| `String` as bytes mangles UI text | `Str.*` indexes Unicode code points; `Str.byteLen` / `Str.byteSlice` keep bytes for framing. Case maps stay ASCII |
| LSP span misses the token | One JSON schema. Check diagnostics and LSP use recorded spans. The dogfood IDE consumes that schema |
| Sim becomes Mockito | Only qualified IO replacements. No stubbing pure `View`/`Signal`. Kits stay TestRuntime |
| Drivers become integration tests | `check` rejects `Property.*` in scenario files. Correctness lives in `*.scuzz_verify` and live-module `.require` |
| Drivers pass vacuously | `Property.sometimes` reachability fails the campaign when declared states are never reached |
| Verification tool sprawl | One `scuzz` strategy — mutation/fuzz/properties/sim/determinism in-tree. No external test frameworks |
| Slow fuzz inner loop pushes authors back to ad-hoc testing | Corpus-only replay tier answers in seconds. Full campaigns stay in CI |
| `Property.sometimes` verdicts vary with iteration budget | Checked-in corpus keeps reaching prefixes. Summary separates never-reached from not-reached-in-budget |
| Concrete business facts have no home without unit tests | Concrete-fact `.require` checks are sanctioned oracles. Zero-argument verify oracles seed the campaign |
| “Almost Scala” confusion | Explicit non-goals. Language direction: [`philosophy.md`](philosophy.md). Run `scuzz docs language`. |
| Watch confused with hot reload | `scuzz watch` rebuilds. `[ui]` `run --watch` is hot reload (stamp-reload Views). IO-only `run --watch` kills and reruns |
| IDE typer ≠ batch typer | One JSON schema. LSP wraps `scuzz check`. No second typer |
| Dogfood IDE before editor primitives | `scuzz ide` launches the bundled editor (`examples/editor`). Headless stays a peer. Do not add a `scuzz-ide` binary |
| Skia weight | pinned CPU prebuilt default. `sk_sw` opt-out |
| Desktop-only features | Headless peer rule. One input alphabet for editor keys, caret, selection, clipboard, compose, and inject |
| Treating UI as the only product | UI is a primary path (Flutter-shaped). CLI/server/desktop/mobile are peers |
| GC vs frame budget | `pump` boundary. Measure |
| Mobile packaging | Host Mobile peer first. `scuzz package` drives Android/iOS shells. Hardware USB later |
| Self-hosting stalls or forks the toolchain | Bootstrap fetches the newest GitHub `v*` release. The Scuzz CLI is the product. No dual toolchains |
