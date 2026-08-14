# Short-term plan

## Next

**Drop a failed request handler and keep `Net.serve` listening** — timed-out, malformed, and reset clients no longer kill persistent `Net.serve`. If the handler IO fails, the whole server still dies. Next: close that connection and accept the next request.
