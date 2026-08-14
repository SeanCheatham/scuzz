# Short-term plan

## Next

**Drop a malformed `Net.serve` client and keep listening** — a timed-out client no longer kills persistent `Net.serve`. A peer that closes without a GET still fails the whole server with `expected HTTP GET`. Next: close that connection and accept the next request.
