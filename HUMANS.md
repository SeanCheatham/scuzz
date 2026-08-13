# Human-Written Notes

This file may be read by AI agents, but it must never be written, edited, or removed by AI agents. This file should be treated as a source-of-truth for everything else.

## Project Goals

- Scuzz is heavily inspired by Scala and ZIO.
- Scuzz is also heavily inspired by Flutter to support GUI-based applications.
- Scuzz does not embrace the JVM; it instead embraces "native".
- Scuzz is meant for building server, CLI, desktop, and mobile apps. Web apps might come at some point.
- Scuzz is LLM/AI-friendly. Scala is especially productive and terse, making it very token-efficient.
- Scuzz includes headles mode, hot reload, and debugging tools, with a particular goal of aiding AI agents
- Scuzz includes the compiler and tooling.
- Scuzz is heavily-opinionated. One way to do things (when practical). One formatter. One linter. One testing strategy.
- Scuzz is "batteries-included"; most use-cases should be supported out-of-the-box without a sprawl of ecosystem libraries.
- Scuzz does not embrace classical unit testing or example-based testing. It prefers mutation, fuzzing, property-oriented, simulation, and determinism instead. These are all built into the language and tooling with first-class support.
- Scuzz leans on a strong compiler with rigid guardrails and constraints.
- Scuzz is heavily functional-oriented but isn't overly pedantic or academic; pragmatism matters heavily too.