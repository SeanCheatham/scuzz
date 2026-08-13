# Short-term plan

## Next

**Nonblocking Net poll.** HUMANS Headless + stamp-watch + live dump + live inject + `Net.serve` persistent listen are in. Live accept still blocks the fiber. Next: poll so a server can share the run loop with other IO. Still not Flutter VM patching.
