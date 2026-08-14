# Short-term plan

## Next

**Request read timeout for `Net.serve`** — `Net.httpGet` bounds DNS, connect, and response read at 1s. A client that connects and never sends a GET still parks the server fiber on request read. Next: fail that wait after a bounded wait.
