# Runtime and verification resource checks

## Behavior

Run the C runtime IO and UI checks with UndefinedBehaviorSanitizer.
Reject a known signed overflow before the runtime checks.
Missing support or disabled instrumentation fails the requested check.
Keep instrumented binaries and relative fixtures in a separate directory.

Keep shared text identity for compiled and evaluator coverage records.
Use a text index to keep required workloads within the probe limit.
Encode complete source inputs as JSON with direct tail recursion.
Keep all workload, memory, stack, and time limits.

## Checked scope

Normal, ASan, and UBSan runtime IO and UI checks pass on Linux.
Coverage checks use 4097 keys, equal text at different pointers, collisions, and resets.
The code-generation corpus and required idle workload pass.
The heap equality fact checks a Boolean guard without unsafe operand display.
Native and evaluator replay checks pass for the affected code-generation examples.

The product CLI uses the current compiler and runtime.
The exact compiler fixed-point check passes.
Kernel integration checks pass.
The native JSON proof checks 245760 input bytes and 876547 output bytes.
It checks control bytes and UTF-8 with an independent expected result.
It uses an 8 MiB stack, a 512 MiB Linux memory limit, and a 20-second deadline.

The runtime sanitizer proof covers C runtime code, tests, and reload libraries.
It does not cover the Skia wrapper, prebuilt Skia, or emitted LLVM code.
Other hosts need their own sanitizer proof.
Finite checks do not prove all programs correct.

Mutation status, input identity, recorded choices, author rule execution,
workload status, claim reach, and verification scope controls pass on both engines.
Counter, Studio, Kernel, Scale, and formatter campaigns pass.
The independent compiler proof passes 54 cases and 12 module checks.
The full generated-program campaign remains open.

Delta integration checks pass.
The broken Counter handler changes the two tap workloads.
Idle and the zero-increment workload remain equal.
Witness replay rejects the broken version and accepts the baseline.
The per-probe limits remain unchanged.

## Remaining checks

- Finish the generated-program campaign with the current compiler.
- Finish the full fuzz integration checks with the current sources.
- Keep the three unreached Editor conditional claims and four unreached UI triggers explicit.
- Use the seven improvement topics in `vision.md` to select the next slice.
