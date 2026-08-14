# Short-term plan

## Next

**Keep `Net.serve` listening after a timed-out client** — request read and response write each wait at most 1s, but that error still tears down the whole server. Next: close the stalled connection and accept the next request so one idle client cannot kill a persistent `Net.serve`.
