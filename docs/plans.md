# Short-term plan

## Next

**`Net.serve` IPv6 listen** — `Net.httpGet` reaches IPv6 via literals and AAAA. `Net.serve` still binds IPv4 `127.0.0.1` only. Next: listen on `::1` (or dual-stack) so Headless/local servers match the client.
