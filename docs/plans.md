# Short-term plan

## Next

**Drop a failed `Net.serve` write and keep listening** — timed-out and malformed clients no longer kill persistent `Net.serve`. A client that resets during the response still fails the whole server with `write failed`. Next: close that connection and accept the next request.
