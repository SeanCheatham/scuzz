# Short-term plan

## Next

**DNS query timeout for `Net.httpGet`** — TCP connect waits at most 1s. A nameserver that never answers still parks on UDP recv until the OS gives up. Next: fail DNS after a bounded wait.
