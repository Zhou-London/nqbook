"use client";

// The dashboard: subscribes to /api/metrics (SSE), keeps a rolling 60 s of
// samples, and plots cumulative counters as per-second rates. Each sample is
// stamped with Date.now() on arrival; its counter increments enter a sliding
// 1 s wall-clock window (lib/rate.ts), whose sum is the rate drawn. Gauges
// (instruments, resting orders, memory) are drawn as received. All series
// wear the one accent hue; status colors mark only the connection state and
// the dropped counter.

import { useEffect, useMemo, useRef, useState } from "react";

import type { MetricsSample } from "@/lib/metrics";
import { RATE_WINDOW_MS, SlidingRate } from "@/lib/rate";

/** Samples kept: 60 s at the publisher's 10 Hz. */
const WINDOW = 600;

/** The cumulative counters in a sample; everything else is a gauge. */
const COUNTER_KEYS = [
  "feedMessages",
  "feedBytes",
  "feedOrders",
  "feedTrades",
  "feedLevels",
  "feedDropped",
  "bookEvents",
  "bookApplyNs",
  "bookSamples",
  "writerOrders",
  "writerTrades",
  "writerLevels",
  "writerBooks",
] as const;

type CounterKey = (typeof COUNTER_KEYS)[number];

/** Windowed per-second rates derived from one sample's arrival, plus the
 * mean apply time (windowed ns per windowed timed apply). */
type RatePoint = Record<CounterKey, number> & { bookApplyMeanNs: number };

type Status = "connecting" | "live" | "stalled" | "offline";

function useMetricsStream() {
  const [samples, setSamples] = useState<MetricsSample[]>([]);
  const [rates, setRates] = useState<RatePoint[]>([]);
  const [status, setStatus] = useState<Status>("connecting");
  const [endpoint, setEndpoint] = useState("");
  const lastArrival = useRef(0);

  useEffect(() => {
    const windows = Object.fromEntries(
      COUNTER_KEYS.map((key) => [key, new SlidingRate()]),
    ) as Record<CounterKey, SlidingRate>;
    let prev: MetricsSample | null = null;
    let streamStart = 0;

    const source = new EventSource("/api/metrics");
    source.addEventListener("hello", (event) => {
      setEndpoint(JSON.parse((event as MessageEvent).data).endpoint);
    });
    source.onmessage = (event) => {
      const sample: MetricsSample = JSON.parse(event.data);
      const now = Date.now();
      lastArrival.current = now;
      setStatus("live");
      setSamples((old) => [...old.slice(-(WINDOW - 1)), sample]);

      if (prev !== null) {
        // A counter below its previous value means nqbook restarted; the
        // post-restart count is the increment.
        for (const key of COUNTER_KEYS) {
          windows[key].push(now, sample[key] >= prev[key] ? sample[key] - prev[key] : sample[key]);
        }
        // Until the stream is a full window old, scale the partial window up
        // to a second so rates don't ramp in from zero.
        const span = Math.min(RATE_WINDOW_MS, Math.max(1, now - streamStart));
        const scale = 1000 / span;
        const point = Object.fromEntries(
          COUNTER_KEYS.map((key) => [key, windows[key].total(now) * scale]),
        ) as RatePoint;
        const applies = windows.bookSamples.total(now);
        point.bookApplyMeanNs = applies > 0 ? windows.bookApplyNs.total(now) / applies : 0;
        setRates((old) => [...old.slice(-(WINDOW - 1)), point]);
      } else {
        streamStart = now;
      }
      prev = sample;
    };
    source.onerror = () => setStatus("offline");

    // The publisher ticks every 100 ms; a second of silence means the
    // pipeline stopped even if the SSE socket is still up.
    const stale = setInterval(() => {
      setStatus((current) =>
        current === "live" && Date.now() - lastArrival.current > 1500 ? "stalled" : current,
      );
    }, 500);
    return () => {
      clearInterval(stale);
      source.close();
    };
  }, []);

  return { samples, rates, status, endpoint };
}

/** One field of every point, in arrival order. */
function series<T extends Record<keyof T, number>>(points: T[], key: keyof T): number[] {
  return points.map((point) => point[key]);
}

const fmtCount = (n: number): string => {
  if (n >= 1e9) return `${(n / 1e9).toFixed(2)}B`;
  if (n >= 1e6) return `${(n / 1e6).toFixed(2)}M`;
  if (n >= 1e4) return `${(n / 1e3).toFixed(1)}k`;
  return n >= 100 || Number.isInteger(n) ? n.toFixed(0) : n.toFixed(1);
};

const fmtBytes = (n: number): string => {
  if (n >= 1 << 30) return `${(n / (1 << 30)).toFixed(2)} GB`;
  if (n >= 1 << 20) return `${(n / (1 << 20)).toFixed(2)} MB`;
  if (n >= 1 << 10) return `${(n / (1 << 10)).toFixed(1)} KB`;
  return `${n.toFixed(0)} B`;
};

const fmtNs = (n: number): string => {
  if (n >= 1e6) return `${(n / 1e6).toFixed(2)} ms`;
  if (n >= 1e3) return `${(n / 1e3).toFixed(1)} µs`;
  return `${n.toFixed(0)} ns`;
};

// A single-series line with a hover crosshair. Points are drawn oldest to
// newest across the full width; the y-scale is 0-or-min to max with a little
// headroom, so a flat series still reads as a line, not an artifact.
function Sparkline({
  points,
  height = 44,
  format,
}: {
  points: number[];
  height?: number;
  format: (v: number) => string;
}) {
  const [hover, setHover] = useState<number | null>(null);
  const width = 100;

  const { path, area, min, max, ys } = useMemo(() => {
    if (points.length < 2) return { path: "", area: "", min: 0, max: 0, ys: [] as number[] };
    const lo = Math.min(0, ...points);
    const hi = Math.max(...points);
    const span = hi - lo || 1;
    const pad = span * 0.08;
    const y = (v: number) => height - 3 - ((v - lo + pad * 0.5) / (span + pad)) * (height - 6);
    const x = (i: number) => (i / (points.length - 1)) * width;
    const ys = points.map(y);
    const path = points.map((_, i) => `${i ? "L" : "M"}${x(i).toFixed(2)},${ys[i].toFixed(2)}`).join("");
    const area = `${path}L${width},${height}L0,${height}Z`;
    return { path, area, min: lo, max: hi, ys };
  }, [points, height]);

  if (points.length < 2) return <div style={{ height }} />;

  const index = hover === null ? null : Math.min(points.length - 1, Math.max(0, hover));
  return (
    <div
      className="spark"
      style={{ height }}
      onMouseLeave={() => setHover(null)}
      onMouseMove={(event) => {
        const rect = event.currentTarget.getBoundingClientRect();
        setHover(Math.round(((event.clientX - rect.left) / rect.width) * (points.length - 1)));
      }}
    >
      <svg
        viewBox={`0 0 ${width} ${height}`}
        preserveAspectRatio="none"
        style={{ display: "block", width: "100%", height }}
        aria-label={`last minute, min ${format(min)}, max ${format(max)}`}
      >
        <path d={area} fill="var(--accent-wash)" opacity={0.45} />
        <path d={path} fill="none" stroke="var(--accent)" strokeWidth={1.4} vectorEffect="non-scaling-stroke" />
        {index !== null && (
          <line
            x1={(index / (points.length - 1)) * width}
            x2={(index / (points.length - 1)) * width}
            y1={0}
            y2={height}
            stroke="var(--ink-3)"
            strokeWidth={1}
            vectorEffect="non-scaling-stroke"
          />
        )}
      </svg>
      {index !== null && (
        <>
          <div
            style={{
              position: "absolute",
              left: `${(index / (points.length - 1)) * 100}%`,
              top: `${(ys[index] / height) * 100}%`,
              width: 8,
              height: 8,
              borderRadius: "50%",
              background: "var(--accent)",
              border: "2px solid var(--card)",
              transform: "translate(-50%, -50%)",
              pointerEvents: "none",
            }}
          />
          <div
            className="tooltip"
            style={{ left: `${(index / (points.length - 1)) * 100}%`, top: -4 }}
          >
            {format(points[index])} · {((points.length - 1 - index) / 10).toFixed(1)}s ago
          </div>
        </>
      )}
    </div>
  );
}

function Tile({
  label,
  points,
  format,
  unit,
  wide = false,
  height,
}: {
  label: string;
  points: number[];
  format: (v: number) => string;
  unit?: string;
  wide?: boolean;
  height?: number;
}) {
  const latest: number | null = points.length ? points[points.length - 1] : null;
  return (
    <div className={wide ? "tile wide" : "tile"}>
      <div className="label">{label}</div>
      <div className="value">
        {latest === null ? "—" : format(latest)}
        {unit && <span className="unit">{unit}</span>}
      </div>
      <Sparkline points={points} format={format} height={height ?? (wide ? 120 : 44)} />
    </div>
  );
}

const STATUS_TEXT: Record<Status, string> = {
  connecting: "connecting…",
  live: "live",
  stalled: "stream stalled",
  offline: "bridge offline",
};

export default function Page() {
  const { samples, rates, status, endpoint } = useMetricsStream();

  const r = useMemo(
    () => ({
      feedMessages: series(rates, "feedMessages"),
      feedBytes: series(rates, "feedBytes"),
      feedOrders: series(rates, "feedOrders"),
      feedTrades: series(rates, "feedTrades"),
      feedLevels: series(rates, "feedLevels"),
      feedDropped: series(rates, "feedDropped"),
      bookEvents: series(rates, "bookEvents"),
      bookApplyMeanNs: series(rates, "bookApplyMeanNs"),
      bookSamples: series(rates, "bookSamples"),
      writerOrders: series(rates, "writerOrders"),
      writerTrades: series(rates, "writerTrades"),
      writerLevels: series(rates, "writerLevels"),
      writerBooks: series(rates, "writerBooks"),
    }),
    [rates],
  );
  const g = useMemo(
    () => ({
      bookInstruments: series(samples, "bookInstruments"),
      bookRestingOrders: series(samples, "bookRestingOrders"),
      bookMemoryBytes: series(samples, "bookMemoryBytes"),
    }),
    [samples],
  );

  const last = samples[samples.length - 1];
  const statusClass = status === "live" ? "live" : status === "stalled" ? "stalled" : "offline";

  return (
    <main>
      <header className="top">
        <h1>nqbook monitor</h1>
        <span className={`pill ${statusClass}`}>
          <span className="dot" />
          {STATUS_TEXT[status]}
        </span>
        {endpoint && <span className="endpoint">{endpoint}</span>}
      </header>

      {rates.length < 2 ? (
        <div className="waiting">
          waiting for samples from the metrics stream
          {status === "offline" && " — is the dev server's ZMQ bridge running?"}
        </div>
      ) : (
        <>
          <h2 className="section">Feed</h2>
          <div className="grid">
            <Tile label="Feed messages" points={r.feedMessages} format={fmtCount} unit="/s" wide />
            <Tile label="Feed bytes" points={r.feedBytes} format={fmtBytes} unit="/s" />
            <Tile label="Feed orders" points={r.feedOrders} format={fmtCount} unit="/s" />
            <Tile label="Feed trades" points={r.feedTrades} format={fmtCount} unit="/s" />
            <Tile label="Feed levels" points={r.feedLevels} format={fmtCount} unit="/s" />
            <Tile label="Dropped frames" points={r.feedDropped} format={fmtCount} unit="/s" />
          </div>

          <h2 className="section">Book</h2>
          <div className="grid">
            <Tile label="Book events" points={r.bookEvents} format={fmtCount} unit="/s" wide />
            <Tile label="Apply time (mean)" points={r.bookApplyMeanNs} format={fmtNs} />
            <Tile label="Timed applies" points={r.bookSamples} format={fmtCount} unit="/s" />
            <Tile label="Instruments" points={g.bookInstruments} format={fmtCount} />
            <Tile label="Resting orders" points={g.bookRestingOrders} format={fmtCount} />
            <Tile label="Book memory" points={g.bookMemoryBytes} format={fmtBytes} />
          </div>

          <h2 className="section">Writer</h2>
          <div className="grid">
            <Tile label="Writer orders" points={r.writerOrders} format={fmtCount} unit="/s" />
            <Tile label="Writer trades" points={r.writerTrades} format={fmtCount} unit="/s" />
            <Tile label="Writer levels" points={r.writerLevels} format={fmtCount} unit="/s" />
            <Tile label="Writer books" points={r.writerBooks} format={fmtCount} unit="/s" />
          </div>

          <details className="raw">
            <summary>latest raw sample</summary>
            <table>
              <tbody>
                {Object.entries(last).map(([key, value]) => (
                  <tr key={key}>
                    <th>{key}</th>
                    <td>{value.toLocaleString()}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          </details>
        </>
      )}
    </main>
  );
}
