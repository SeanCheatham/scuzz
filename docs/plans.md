# Short-term plan

## Next

**Response write timeout for `Net.serve`** — request read waits at most 1s. A client that never reads the response still parks the server fiber on write. Next: fail that wait after a bounded wait.
