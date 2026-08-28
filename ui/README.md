# nqbook-ui

<p align="center">
  <img alt="Next.js 15" src="https://img.shields.io/badge/Next.js-15-000000?logo=nextdotjs&logoColor=white" />
  <img alt="React 19" src="https://img.shields.io/badge/React-19-61DAFB?logo=react&logoColor=black" />
  <img alt="TypeScript 5" src="https://img.shields.io/badge/TypeScript-5-3178C6?logo=typescript&logoColor=white" />
  <img alt="ZeroMQ SUB" src="https://img.shields.io/badge/ZeroMQ-SUB-DF0000?logo=zeromq&logoColor=white" />
  <img alt="Transport: Server-Sent Events" src="https://img.shields.io/badge/transport-SSE-4c1" />
  <img alt="Runs on the host" src="https://img.shields.io/badge/runs-on%20the%20host-blue" />
</p>

Live dashboard for `nqbook`'s metrics stream. A Next.js server route
subscribes to the service's conflating ZMQ PUB socket, decodes each 136-byte
`nlib::metrics` record, and relays it to the browser over Server-Sent Events;
the page keeps a rolling minute of samples and plots counters as per-second
rates over a sliding 1 s wall-clock window. White surface, one blue for every
series; status colors mark only the connection state and the dropped counter.

![The dashboard on a live Kraken feed](docs/dashboard.png)

```bash
npm install
npm run dev                  # http://localhost:3000
NQBOOK_METRICS_ENDPOINT=tcp://127.0.0.1:5556 npm run dev   # the default
```

Run the stack first: `apps/util`'s `md/kraken` publishing on 5555, `nqbook`
in the dev container with `-p 5556:5556` so its metrics socket is reachable
from the host. This app runs on the host with Node, not in the container, and
both `npm run dev` and `npm run start` pin port 3000.

## Shape

```
lib/metrics.ts            nlib::metrics decoder (136 bytes, little-endian)
lib/rate.ts               SlidingRate: windowed sum over arrival timestamps
app/api/metrics/route.ts  SSE bridge: one conflated ZMQ SUB per client
app/page.tsx              the dashboard: rolling window, rates, sparklines
app/globals.css           the white/blue theme tokens
```

- **The browser never speaks ZMQ.** The bridge runs in the Next.js Node
  process (`zeromq` npm package, prebuilt libzmq) and holds one `SUB` per SSE
  client, `conflate` set like the publisher's, so a tab that lags skips to
  the newest sample instead of replaying a backlog.
- **The page turns counters into rates.** The stream carries cumulative
  counters and gauges. Each sample is stamped with `Date.now()` on arrival;
  counter increments enter a sliding 1 s wall-clock window (`lib/rate.ts`: a
  deque of `(timestamp, increment)` pairs with an incrementally maintained sum
  — amortized O(1) per sample, memory one window deep), and the windowed sum
  is the per-second rate plotted. Apply time is shown as the windowed mean per
  timed apply; gauges are plotted verbatim. A counter that goes backwards is
  nqbook restarting — the window absorbs it as a reset, not a negative rate.
- **Tiles are grouped by stage** — feed, book, writer — each counter on its
  own tile rather than summed; the raw sample stays available under the
  disclosure at the bottom.
- The connection pill distinguishes a dead bridge (SSE error) from a stalled
  pipeline (bridge up, no sample for 1.5 s).
