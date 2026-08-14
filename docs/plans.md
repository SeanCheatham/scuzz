# Short-term plan

## Next

**HTTP read timeout for `Net.httpGet`** — DNS and TCP connect each wait at most 1s. A peer that accepts and never sends still parks on read until the OS gives up. Next: fail the response wait after a bounded wait.
