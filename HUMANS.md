# Human-Written Notes

This file may be read by AI agents, but it must never be written, edited, or removed by AI agents. This file should be treated as a source-of-truth for everything else.

## Project Goals

- Scuzz is heavily inspired by Scala and ZIO.
- Scuzz is also heavily inspired by Flutter to support GUI-based applications.
- Scuzz does not embrace the JVM; it instead embraces "native".
- Scuzz is meant for building server, CLI, desktop, and mobile apps. Web apps might come at some point.
- Scuzz is LLM/AI-friendly. Scala is especially productive and terse, making it very token-efficient. Scuzz aims for a similar degree of efficiency.
- Scuzz includes headless mode, hot reload, and debugging tools, with a particular goal of aiding AI agents
- Scuzz includes the compiler and tooling.
- Scuzz is heavily-opinionated. One way to do things (when practical). One formatter. One linter. One testing strategy.
- Scuzz is "batteries-included"; most use-cases should be supported out-of-the-box with the language and standard library, without a sprawl of ecosystem libraries.
- Scuzz does not embrace classical unit testing or example-based testing. It prefers mutation, fuzzing, property-oriented, simulation, coverage, and determinism instead. These are all built into the language and tooling with first-class support.
  - Developers encode assertions directly in the main codebase. Separate simulation drivers activate various behaviors, and all invariants of the codebase must be satisfied under fuzzing via these drivers.
  - Developers also define temporal verifications in the form of a function `Timeline => Verdict`. Timeline is an algebra into the entire linear history of a fuzzing execution up to some terminal condition. A Verdict indicates if the Timeline is valid or invalid. The function is invoked after fuzzing along some branch of the multiverse.
  - The simulation mechanic should be hermetically sealed. Since all non-determinisms are captured through the effect system, any network effects beyond localhost should be rejected and error accordingly.
  - `scuzz fuzz --iterations <int>` is the primary entrypoint for testing. It runs the fuzzer with coverage and mutation mixed in, until a desired iteration budget is exhausted. Once exhausted, it outputs property evaluation and coverage results.
- Scuzz leans on a strong compiler with rigid guardrails and constraints.
- Scuzz is heavily functional-oriented but isn't overly pedantic or academic; pragmatism matters heavily too.