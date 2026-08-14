# Short-term plan

## Next

**RFC 8305 IPv6 preference delay** — `Net.httpGet` already races A+AAAA connects. Next: wait ~250ms before starting the A connect when AAAA is in hand, so working IPv6 wins on dual-stack names.
