# Current slice: generate toward one objective

Status: implementation in progress.

Implement gate 3 of [`vision.md`](vision.md#3-manage-a-local-model-and-generate-toward-one-objective). Follow the proposal review locks in [`philosophy.md`](philosophy.md#proposal-review).

## Outcome

A session starts with one objective, allowed source paths, mode, and visible limits. Local generation and External proposals use one frozen request and one validated response. The CLI lists and downloads the fixed models. A finite command publishes one suggestion without opening the editor or changing source.

## Scope

Use the existing editor and product CLI. Share the catalog, request, parser, publication, and process lifecycle between the two callers. Use one pinned llama.cpp CPU release. Use the supported Hub downloader and shared immutable cache. Keep the two catalog entries and resource profiles in `vision.md`. Do not add package model settings, a provider framework, or new runtime builtins.

## Work order

1. Verify pinned model metadata and a released CPU backend. Record digests, licenses, templates, and supported host artifacts in one catalog. Prove offline listing and exact shared-cache inspection.
2. Implement explicit download and backend setup. Check disk, memory, context, and deadlines. Keep partial weights unavailable. Keep shared files after cancellation.
3. Add the objective form and frozen request contract. Validate source scope, identities, limits, and complete publication. Use the same parser for external and local responses.
4. Implement one owned inference process on private loopback. Tokenize before generation. Enforce request bounds. Stop owned work on pause, switch, timeout, and exit. Require explicit retry.
5. Add `scuzz models list`, `scuzz models download`, and `scuzz ide generate-suggestion`. Use bounded JSON output and nonzero failure status. Refuse existing output and stale requests.
6. Prove fake effects and finite live process, HTTP, publication, cancellation, and cleanup. Run the opt-in real default model proof. Keep unavailable host and model checks explicit.
7. Update `scuzz docs ide` and the current gaps. Measure generation and readiness. Commit the slice and the next plan after required software proofs pass.

## Acceptance criteria

- [ ] The picker and CLI expose only the two fixed model entries.
- [ ] Model listing works offline without a downloader or workspace writes.
- [ ] Downloads verify the pinned size and digest through the shared cache.
- [ ] Explicit setup shows artifact identity, license, cache, and limits.
- [ ] One objective and allowed source paths bind each bounded request.
- [ ] External import works without model tools and rejects partial, stale, or changed publication.
- [ ] Local and external responses use one strict parser.
- [ ] Context fit is checked before generation. Truncated output fails.
- [ ] Pause, timeout, switch, and exit stop only owned work.
- [ ] The finite command publishes one complete candidate and changes no source.
- [ ] Hermetic and finite live proofs cover both catalog profiles and failure paths.
- [ ] A real default model yields a checked proposal and a behavioral witness, or the unavailable external proof remains explicit.
- [ ] The manual matches setup, limits, commands, and publication.

## Required validation

Rebuild the checkout product CLI after compiler or CLI changes. Use scratch directories for editor and generation proofs. Export the documented `LIBRARY_PATH` for Skia links.

```bash
./scripts/bootstrap.sh
./examples/cli/build/cli check examples/cli
./examples/cli/build/cli check examples/editor
./examples/cli/build/cli fuzz --iterations 0 examples/cli
./examples/cli/build/cli fuzz --iterations 0 examples/editor
./examples/cli/build/cli fuzz --iterations 32 examples/editor
./scripts/ci.sh ui
./scripts/ci.sh delta
./scripts/ci.sh pr
git diff --check
```

Add finite controlled lifecycle and command proofs to the existing CI path. Ordinary CI uses no weights or external network. Run real download and CPU inference only in the explicit opt-in proof. Record revision, backend identity, prompt time, generation time, tokens, throughput, memory, and readiness. Do not replace real inference with controlled replies.

## Completion and continuation

Close implemented gaps after their software criteria pass. Keep model usefulness and human evidence unknown. Replace this plan with gate 4. Commit the slice and next plan. Continue the authorized arc through the complete session endpoint. Do not reduce review limits or remove claims and corpus entries.
