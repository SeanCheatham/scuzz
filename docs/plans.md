# Short-term plan

## Next

**TCP connect timeout for `Net.httpGet`** — dual-stack races and the 250ms IPv6 preference delay are in. A lone blackhole address can still hang until the OS gives up. Next: fail the connect after a bounded wait.
