# Scuzz Lang vision

Open work and implementation order. Design locks: [`philosophy.md`](philosophy.md). Ranked gaps: [`gaps.md`](gaps.md). Current slice: [`plans.md`](plans.md). Platforms and toolchain: [`compatibility.md`](compatibility.md). Later tune work: [`optimization.md`](optimization.md).

Edit this file when the order changes. Remove completed work. Keep only the next slice in `plans.md`.

## Primary arc: development through decisions

A developer sets one objective and reviews a stream of small changes. Each card compares one proposal with the current working tree. Scuzz finds a recorded execution that shows the difference. The developer chooses a blind lane. Accepted changes become the baseline for the next request. Claims constrain the choices. Locks: [`philosophy.md`](philosophy.md#proposal-review).

The first complete feature includes the seven gates below. Implement them in order. A generator transport proof does not prove useful generation. A deterministic session does not prove human preference or speed. Keep those unknowns explicit in `gaps.md`.

## Implementation gates

### 1. Trust each decision

Replace Keep and Reject with Choose left and Choose right. Add Can't decide. Bind every action to a ready card and its fixed lane order. Remove fallback acceptance of a different pending proposal. A repeated action cannot choose the next card.

Freeze all review inputs and source bytes. Invalidate evidence on an external edit, proposal edit, compiler change, or verification change. Clear stale lanes after a failed review. Preserve dirty buffers. Use canonical paths rather than module stems as file identity.

Store snapshots, decisions, and an operation journal under `.scuzz/ide/`. Add recoverable multi-file acceptance and conditional Undo. Remove the build-directory decision log. Reject malformed or unsafe proposal paths. Check absolute candidate verdicts. A candidate that shares a baseline failure cannot reach a preference card.

**Gate:** Both lane orders select the expected bytes. Baseline choice and abstention write no source. Stale, dirty, failed, and duplicate actions write no source. Interrupted acceptance recovers on restart. Recovery and Undo preserve unrelated edits. Records survive deletion of `build/`. Claims and corpus prove these behaviors. A live Headless run exercises real review and writes.

### 2. Ask one behavioral question

Add required corpus replay to idle and seed workloads in IDE review. Reuse differential search and shrinking from `Diff`. Give search an explicit budget. Keep evaluator and emitted meanings aligned. Do not turn a timeout, drift, or incomplete workload set into a successful review.

Open the shortest available divergent witness at its first useful difference. Start paused. Show the trigger and both results. Expand to other workloads, the step rail, and changed source. Show evidence scope and claim trigger reach. Keep structure tiles clearly identified. Remove source-only results from the behavior queue after the configured search ends.

**Gate:** A difference found only by a corpus entry blocks or explains the candidate. Search finds a difference absent from idle and seeds. Shrinking preserves it. A shared claim failure blocks the card. An untriggered claim is visible as such. Both lane orders keep behavior and source aligned. A no-difference result does not claim equivalence. IO and UI packages have compiled witness parity proofs.

### 3. Manage a local model and generate toward one objective

Open the review session on an objective form. Add allowed source paths, Local model and External proposals modes, Start, Pause, and visible work limits. Keep source editing available from the existing landmarks. Implement one managed local backend and directory import. Both supply one validated proposal type. Keep configuration local to the IDE. Do not add model settings to the package manifest. Remove the executable generator and configurable inference endpoint designs.

#### Local model setup

Use llama.cpp for inference and single-file GGUF text models. Start with CPU inference. Pin one tested released backend build with its artifact digest and license information. Use a release that supports the shared Hub cache and required model architecture. Verify release behavior rather than assuming current main documentation applies unchanged. Fetch a host binary during explicit local setup. Keep it in the user tool cache. Do not build an inference runtime during every IDE build or install host packages. Do not require a GPU toolkit. Linux and macOS use the same lifecycle contract. Unsupported hosts retain External proposals mode.

Use the supported `hf` downloader for Hub files and shared-cache locking. Detect a missing command and show one setup instruction. Scuzz invokes it for the selected model; the user does not need to copy files. The model picker offers the two fixed options below. It has no free-form repository, filename, or model-path input. Show cached, downloadable, loading, and unavailable states for each option. Reuse only the catalog snapshot and artifact. Do not browse or rank the full Hub. Use public, ungated, single-file artifacts. No model conversion, custom model code, adapters, or gated-login UI. A model card link gives the license and provenance.

Use this initial catalog. Weight sizes come from the selected repository revisions. Memory budgets are initial process limits, not measured requirements or total host RAM. Both options use Q4_K_M quantization and an Apache 2.0 license.

| Choice | Repository and file | Weight bytes | Inference memory budget |
| --- | --- | --- | --- |
| SmolLM3 3B — default, smaller download | `ggml-org/SmolLM3-3B-GGUF`, `SmolLM3-Q4_K_M.gguf` | 1,915,305,312 | 4 GiB |
| Qwen2.5-Coder 7B Instruct — larger coding model | `Qwen/Qwen2.5-Coder-7B-Instruct-GGUF`, `qwen2.5-coder-7b-instruct-q4_k_m.gguf` | 4,683,073,536 | 8 GiB |

Pin SmolLM3 to `4965cb60b150737b68a0408c36aeefb65078f894`. Pin Qwen to `13fb94bfda8c8cf22497dc57b78f391a9acb426a`. Use the Qwen single-file artifact, not its split variants. Verify the file SHA-256 from the pinned repository metadata. Store the revision, filename, byte count, digest, license link, chat-template settings, and resource profile in one small catalog in the existing editor code. Do not add a model registry or provider framework. Sources: [SmolLM3 GGUF](https://huggingface.co/ggml-org/SmolLM3-3B-GGUF/tree/4965cb60b150737b68a0408c36aeefb65078f894) and [Qwen Coder GGUF](https://huggingface.co/Qwen/Qwen2.5-Coder-7B-Instruct-GGUF/tree/13fb94bfda8c8cf22497dc57b78f391a9acb426a).

The smaller option is the setup default. It is not a quality claim. Require an explicit choice before downloading or loading the larger option. Do not switch models or quantization after a failure. Recheck context and memory on a switch. Prove both chat templates with the pinned backend. Disable extended thinking for the initial SmolLM3 profile through its supported template setting. Reject a reasoning-only or truncated response. Neither option has proven Scuzz generation quality. Do not add Qwen2.5-Coder 3B to this catalog. Its [research license](https://huggingface.co/Qwen/Qwen2.5-Coder-3B-Instruct/blob/main/LICENSE) limits use to non-commercial research or evaluation without a separate commercial license.

Honor `HF_HUB_CACHE`, then `HF_HOME`, then the standard cache location. Resolve a selection to an immutable commit and record it. Download that commit, not a moving branch. Reuse the returned snapshot path without copying weights. Do not allow a backend-specific cache override to redirect the shared model store. Delegate large-file streaming and cache writes to the downloader. Do not read weights into a Scuzz String. Do not delete shared snapshots or blobs. Unregister removes only the IDE selection. Reference: [Hub Local Cache](https://huggingface.co/docs/hub/local-cache) and [Hub CLI](https://huggingface.co/docs/huggingface_hub/main/guides/cli).

Show repository, commit, filename, byte size, license link, cache destination, and limits before Download. Support cancellation and report incomplete downloads as unavailable. Reuse complete cached weights offline. Do not update the selected snapshot on restart. Separate download progress, model loading, and card readiness. Store the resolved model identity and resource settings in local session metadata. Fetching a model does not prove that it produces useful Scuzz code.

#### Command-line model management

Add these commands to the product CLI during this gate:

```bash
scuzz models list
scuzz models download smollm3-3b
scuzz models download qwen2.5-coder-7b
```

`scuzz models list` shows both catalog options. Show stable ID, name, quantization, exact weight size, license link, memory budget, cache location, and cache state. Inspect the local cache without network access, downloads, inference startup, or workspace writes. A missing downloader does not prevent the catalog list. Mark incomplete files as unavailable. Do not list arbitrary compatible files as selectable models.

`scuzz models download <model-id>` explicitly downloads only that catalog artifact at its pinned revision. Apply the shared disk, size, hash, deadline, cancellation, and cache-lock rules. Reuse a complete verified cached artifact offline. Report a missing downloader with the same setup instruction as the IDE. Refuse unknown IDs. Do not accept repository names, moving revisions, arbitrary URLs, or output paths. Do not copy weights into the working tree, select a model for a review session, load inference, or fetch backend binaries. Backend setup remains part of explicit IDE setup or `generate-suggestion --download`.

Give both commands readable output and `--message-format=json` for scripts. Structured results include the catalog identity, revision, file, byte count, cache state, and snapshot path when available. Keep progress on stderr. A successful download exits zero only when the pinned artifact is complete and verified. Errors and interrupted downloads exit nonzero. Preserve completed cache entries. Do not add deletion, model conversion, a Hub browser, or background download services in this arc.

Share catalog data, cache inspection, downloads, and diagnostics with the IDE and finite generation command. Add CLI parsing and hermetic cache-state proofs. Add a controlled live downloader proof for failure and cancellation. Use `scuzz models download smollm3-3b` in the opt-in real CPU setup proof. Ordinary CI downloads no weights and accesses no external network.

#### Inference lifecycle

Launch one owned `llama-server` process with the exact snapshot path, bounded context, CPU settings, and loopback binding. Use a free port and identify the owned process. Handle port conflicts without using an unrelated server. Await health readiness with a deadline. No system daemon remains after the IDE exits. Pause, model switch, and exit cancel generation and stop owned inference. Resume loads the same snapshot. Report a crashed backend; retry is explicit.

Use the Net kit for a private non-streaming request to the owned backend. Keep this transport inside the local implementation. Do not expose endpoint or remote API-key configuration. Use the pinned backend's model chat template to form the prompt. Supply the proposal schema and use supported grammar constraints where practical. Parse and validate the generated content. Reject truncated output, malformed JSON, stale identities, excessive sizes, and edits outside the source set. Do not add tool calling or an agent loop. Reference: [llama.cpp server](https://github.com/ggml-org/llama.cpp/blob/master/tools/server/README.md).

Tokenize the request before generation. Fit the objective, constraints, source, feedback, and output allowance inside the configured context. Reject excess input with a scope-reduction action. Do not silently drop required inputs or let the backend shift them out. Model identity, backend build, context, sampling parameters, and seed are generation provenance. Do not treat a sampling seed as proof of identical inference across hardware.

#### Shared proposals and external producers

The generation request contains a version, request identity, objective, baseline hash, compiler identity, frozen sources, allowed paths, recent feedback, and limits. Include a short Scuzz language and proposal example from the shipped manual. Keep this instruction within the same context budget. Export it as `build/ide/request.json`. It contains no credentials. The proposal response echoes the request identity and baseline hash. It contains a proposal kind, generator identity, and path/content replacements. Reject unknown proposal fields. Use the same parser for local output and imported metadata.

External producers read this request and publish candidate directories under `build/proposals/`. Publish only when replacement files and `proposal.json` metadata are complete. Use a staging directory outside the watched inbox and rename it into the inbox. Metadata contains the request identity, baseline hash, proposal kind, generator identity, and each replacement path and content hash. Import only complete candidates whose hashes match. The IDE watches the inbox; it does not run a producer executable or manage its credentials. Test partial publication, changed contents, duplicate identities, and stale baselines. Manual import uses the same schema.

#### Finite command-line generation

Add `scuzz ide generate-suggestion` in the product CLI during this gate. Keep `scuzz ide` as the interactive entry point. The command reads a frozen request file, selects a catalog model by stable ID, generates one proposal, validates it through the shared importer, publishes one complete candidate directory, and exits. Use `smollm3-3b` and `qwen2.5-coder-7b` as the catalog IDs. Keep request construction and generation operations available to both callers. Do not add a second prompt, model catalog, downloader, parser, or inference lifecycle.

Use this target command shape. The command does not exist until this gate ships.

```bash
scuzz ide generate-suggestion <package> \
  --request <request.json> \
  --model smollm3-3b \
  --out <new-candidate-directory>
```

Validate the request version, source scope, limits, compiler identity, and baseline against the selected package before inference. Recheck the baseline before publication. Use cached weights and backend by default. An explicit `--download` flag permits missing catalog weights and the pinned host backend to download. Apply the same disk, memory, and deadline checks as interactive setup. Never open the UI, seed a workspace, start a watch loop, or change package source and verification files. Refuse an existing output directory. Stage outside the watched inbox and publish through the same complete-directory protocol.

Write one bounded JSON result to stdout. Put progress and diagnostics on stderr. Exit zero only after complete publication. Report the request identity, model identity, candidate path, baseline hash, and measured generation time. A published proposal has passed format, identity, and scope checks. It has not passed behavioral review and is not an accepted change. Report this distinction in the result and manual. Use nonzero exit status for invalid input, unavailable resources, timeout, failed generation, or failed publication. On interruption, remove incomplete output and stop only owned children. Keep a completed shared model download in the cache.

Prove parser errors, stale requests, existing output, cancellation, timeout, and cleanup without model weights. Add a finite live command proof with a controlled backend. Use this command for the opt-in real 3B generation proof. Then import its result into the finite Headless review proof to check claims and find a behavioral witness. Do not expose acceptance, Undo, or rule installation through an unattended command in this arc.

#### Initial limits and proof

Start with these limits: three changed files, 64 KiB of replacement source, 1 MiB of serialized input, 128 KiB of response, 4096 context tokens, up to 1024 output tokens, 15 minutes per generation request, eight requests per session, 32 differential search iterations per candidate, and two cards including the displayed card. Include at most 16 recent feedback records. Adapt the output allowance to the actual model context. Refuse incompatible settings. Default to one loaded model, one download, and one generation at a time.

Use separate setup limits: the exact catalog weight byte count, a 60-minute download deadline, a five-minute load deadline, and the selected profile's inference memory budget. Check free disk space for weights and download overhead before Download. Inspect available host memory before loading. Leave memory for the IDE and operating system. Do not load the larger option if the configured budget does not fit. Estimate model and context memory before loading. Enforce the process budget where the host supports it and report any limit the host cannot enforce. Do not apply the target probe's 512 MiB virtual-memory cap to model inference. Show and validate limits before setup and Start. No automatic model downloads at IDE boot or in ordinary CI. Model verification uses an explicitly selected artifact in a scratch workspace. Keep mutation generation as a diagnostic control.

Keep generation and model setup outside the target simulation. Scenario proofs fake process, Net, and download effects. Ordinary CI uses no model weights and no external network. Live finite Headless proofs exercise a controlled child, private loopback, filesystem publication, cancellation, and failure recovery. A separate opt-in proof uses the real downloader, pinned backend, and one small model. Use the default 3B option for the first real CPU proof. Exercise both catalog entries in controlled lifecycle proofs. Run a real 7B proof only when host resources permit it. Record unavailable or untested options separately. Do not expand the catalog during the overnight arc to search for a better model. Measure actual Scuzz output; do not choose from model size alone.

Expect CPU generation to take minutes on a laptop without a GPU. Keep elapsed time, the deadline, and Cancel visible during loading and generation. Keep navigation and Pause responsive while the request runs. Start with at most four CPU threads, bounded by available host threads. Measure prompt processing time, generation time, output tokens, tokens per second, and observed memory after each real run. Do not promise a card rate before measurement. A generation timeout consumes its request budget and requires explicit retry. Slow inference cannot weaken candidate checks or increase the queue without a bound. Keep context and output settings explicit; require enough space for one complete small-file proposal.

**Gate:** The picker and CLI offer only the two catalog entries. CLI listing works offline. CLI download and generation terminate with structured results and leave no owned child. Cached selection and explicit download use the shared cache. Interrupted setup remains recoverable and never loads partial weights. A fake lifecycle proves startup, loading, generation, pause, resume, switch, and exit. Live proofs leave no owned process behind and preserve unrelated processes. External import works with no model, backend, or download tool. A real local model produces a checked proposal and a behavioral witness toward an objective. Report unavailable hardware or artifacts as unverified. Controlled replies do not prove model usefulness.

### 4. Keep the next card ready

Prepare one successor while the developer reviews the current card. Reuse baseline checking and replay where the existing compiler permits it. Cache by full review identity. Keep one local generation request and one review job at most. Bound inbox inspection and import to the ready queue capacity. Generation and review can run while navigation remains responsive.

After acceptance, Undo, rule installation, objective change, or model change, cancel stale work and refill from the new baseline. A successor prepared for the old baseline is usable after baseline choice or abstention only. Show preparation status while the new baseline has no ready card. Do not reuse old replacement files as new proposals. Suppress duplicate source sets and observed outcomes within one baseline and objective. End the queue with a visible status when the request budget is exhausted.

**Gate:** Cached card, workload, and step navigation starts no probe, build, or subprocess. A late response cannot replace the displayed card or enter the next baseline. No owned job remains after pause or exit. Pause stops local generation, owned inference, and candidate preparation. It does not stop independent producers. Leave new inbox candidates pending until resume. Repeated Generate actions cannot exceed queue or request limits. Measure cold readiness, warm readiness, and navigation separately. Target warm cached navigation below 250 ms on the measurement host. Record an unmet target; do not weaken checks to reach it.

### 5. Learn from local choices

Send bounded recent preferences, abstention reasons, and Undo results with local model requests and exported generation requests. Keep candidate rationale hidden before a blind choice. Show the reveal and a short result after the choice. Let the developer retain region focus or select another region.

Add a local session summary. Report accepted choices, baseline choices, abstentions by reason, automatic exclusions, duplicates, Undo, readiness time, and review time. For the first metric, count an acceptance as retained only when it is not undone and its touched files still match its accepted bytes at session end. Report this as a conservative source measure. It does not establish that later edits preserve or remove the intended behavior. Calculate retained acceptances per active review minute. Active review time starts when a ready card appears and ends at its choice or abstention. Exclude paused time and generation waits. Report waiting time separately. Keep source hashes so this measure is auditable. Do not add telemetry or model training.

**Gate:** Skip does not become a negative preference. Automatic exclusions do not count as human choices. Undo and later replacement change the retention result. The next request reflects the current baseline and bounded feedback. Session records reproduce the displayed totals. A reveal cannot expose the next card's lane mapping.

### 6. Offer a rule from a preference

Add an explicit Suggest rule action for a recorded choice. Use the same local model path or external import with a rule request kind. External mode exports the rule request and waits for a matching published draft. A rule response contains added claim source and the supporting witness identity. It can add a root `*.scuzz_verify` file or add claims to one existing verify file. It cannot alter existing definitions or the scenario.

Show rule source and the witness in a separate rule review. Check and replay all required workloads with the rule. Install only after the developer selects Install rule. Use the same stale-input, dirty-buffer, journal, and Undo checks. Invalidate behavior cards after installation. A failed or unsupported draft stays a draft with a diagnostic.

**Gate:** A behavior choice never writes verification source. Rule installation needs its own action. A rejected rule changes no file. A weakened existing claim is rejected. An untriggered new claim cannot be offered as verified. Installation and Undo preserve existing claims and recover after interruption. The installed rule constrains a later behavior proposal. Finite evidence does not become a universal proof label.

### 7. Prove and refine the complete session

Use existing examples for the proof. Exercise one UI objective and one IO objective in scratch workspaces. Include generation, candidate checks, a short witness, each lane choice, abstention, next-baseline generation, Undo, rule review, restart, and a session summary. Include invalid, stale, and interrupted operations. Keep the simulation proof hermetic. Add live finite Headless proofs for the real local process, private HTTP, and filesystem paths.

Run the required checks in `plans.md` for each slice. Update `scuzz docs ide` when behavior ships. Measure review latency and queue behavior on the same host with the same limits. Make at most two measured refinement passes after the full session works. Each pass addresses an observed failure, delay, or confusing control. Rerun affected proofs. Do not add unrelated language or editor features.

**Gate:** All functional gates pass, the product CLI is rebuilt, and required CI passes. The manual matches the shipped controls and protocol. It gives managed model setup, shared-cache behavior, resource limits, the model list and download commands, the finite generation command, an external publication recipe, and sample request and response. Keep these instructions provider-independent. Record current measurements and remaining external or human validation in `gaps.md`. Human evidence requires a real review session; do not invent keep rates or preferences. If configuration is unavailable, report the real-generator check as unverified. Do not claim the complete feature passes all gates.

## Execution boundary

Use `plans.md` for the first remaining gate. Complete its software proofs before advancing. Keep unavailable external and human checks explicit. They do not prevent independent later implementation work. Remove the completed slice from this file and from `gaps.md`. Delete its `plans.md`. If arc work remains, create the next slice from this order and continue. Do not wait for a new instruction between authorized slices.

Change compiler or runtime code only for a demonstrated blocker or a required shared review operation. Keep one clear implementation. Do not rewrite the compiler or editor for architecture alone. Preserve the newest-release bootstrap constraints. Do not reduce probe limits, skip claims, remove corpus entries, or bypass replay failures to finish.

Unavailable local model resources or human availability do not prevent deterministic, loopback HTTP, and directory import proofs. They remain explicit completion limits. On a hard blocker, preserve a working subset and leave `plans.md` with remaining criteria and a reproducer. Do not start unrelated work or mark an incomplete gate complete. Stop implementation at the complete-session endpoint. Human usefulness remains an unknown until a developer session measures it.

## Supporting work and exclusions

Compile time and evaluator parity support this arc. Measure changes that affect them. Standard kits follow when an ordinary program or this review loop needs them. Ranked work: [`gaps.md`](gaps.md).

Do not start FFI, a registry, plugins, provider SDKs, a remote generator service, model training, physical-device packaging, or `*.scuzz_tune`. Keep one CPU inference backend and one model format. General Hub browsing, GPU variants, gated-model login, model conversion, and training stay later. Do not add pixel preview infrastructure, semantic timeline alignment, social accounts, public feeds, ranking algorithms, or swipe animation frameworks. Keep keyboard and shared Headless input as peers. Native file dialogs, menus, multi-window, debugger, and general editor expansion stay later.

## Risks

| Risk | Required control |
| --- | --- |
| A blind control selects an unseen state | Select a lane by fixed card identity |
| Acceptance writes bytes other than those reviewed | Freeze inputs and revalidate before writing |
| A multi-file write stops halfway | Journal, restart recovery, and conditional Undo |
| A proposal passes because both sides fail | Check absolute candidate verdicts |
| A narrow workload misses a consequence | Corpus, bounded search, and visible evidence scope |
| A claim never triggers | Show trigger reach; do not call an untriggered rule verified |
| A generator rewrites accepted work | Bind requests to a baseline and discard stale results |
| The stream consumes unlimited work | Explicit limits, bounded queues, and cancellation |
| Model setup expands into an inference platform | One pinned CPU backend, two catalog entries, one format, and explicit setup limits |
| Model weights duplicate or damage another tool's cache | Use the shared downloader and immutable snapshots; do not prune |
| A local model cannot generate useful Scuzz changes | Measure a small model through the same behavioral gates |
| A model consumes the target probe budget | Separate inference limits from correctness probe limits |
| An external candidate is read before publication ends | Complete metadata, content hashes, and one importer |
| Structural tiles imply pixel fidelity | Label the view as recorded structure |
| A quick choice does not reflect a useful preference | Measure retention and obtain human evidence |
| Evaluator and emitted behavior differ | Shared witness replay; preserve parity failures |
| Review cost makes the loop unusable | Measure readiness separately from cached navigation |
