# Replay result evidence

## Behavior

Each replay writes a report for its checked workload.
The report names the replay input and execution mode.
Retain the script, seeds, declared choices, and executed choices.
Keep author rule execution separate from execution success.
Use the existing workload verdict rules.
Report terminal rule rejection as failed.
Report a deadline, step limit, or tool failure as incomplete.
Report an excluded required input as excluded and return failure.
Replace a previous report before source checking or building starts.
A successful replay must not leave an incomplete report.
One replay does not establish that a full campaign passes.

## Work

- Write the final replay report before returning the command result.
- Give replay reports an explicit command kind and scope.
- Preserve native replay, live, session, and sanitizer modes.
- Make the console and JSON verdict agree.
- Update the author manual and design limits.

## Required checks

- Check passed, rejected, excluded, and incomplete replay reports.
- Check an unguarded passing replay and actual author rule names.
- Retain recorded scheduler choices in the replay report.
- Replace stale success before a source, build, or input failure.
- Check simulation and live replay with the same report rules.
- Preserve campaign reports and required workload counts.
- Rebuild the product CLI and run affected integration checks.

## Status

The replay path writes one final workload report.
The console and report use the existing workload verdict rules.
Focused checks pass for all four verdicts and three execution modes.
Source, link, and missing-input failures replace previous success.
Recorded choices and executed author rule names remain visible.
The 20-second deadline reports incomplete and returns failure.
The rebuilt product includes the manual report fields and limits.
The compiler source check passes.
The compiler fixed-point check passes.
The integration run passes the replay and deadline report checks.
The remaining verification integration checks run.
Live application IO, sanitizer mode, and campaign preservation remain open.
