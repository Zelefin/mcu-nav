import {
  Cable,
  Bug,
  FlaskConical,
  HardDrive,
  PlugZap,
  RotateCcw,
  Save,
  Satellite,
  Unplug,
} from "lucide-react";
import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import {
  type CaptureDataRecord,
  type LogRecord,
  type NodeQualityRecord,
  type RangeRecord,
  type SnapshotPayload,
  type SnapshotPeer,
  isNodeId,
  parseFirmwareBuild,
  parseInboundLine,
} from "./lib/controlRecords";

const BAUD_RATE = 115200;
const NODE_NAMES_KEY = "nav-mcu.nodeNames.v1";
const DASH = "-";

interface Observation {
  aId: number;
  bId: number;
  fromId: number | null;
  toId: number | null;
  requestId: number | null;
  rangeMm: number | null;
  rangeSigmaMm: number | null;
  rssi: number | null;
  snr: number | null;
  failReason: string | null;
  source: string;
  state: "ok" | "fail" | "stale" | "missing" | "unknown";
  note: string;
  updatedAt: number | null;
}

interface SerialLogEntry {
  id: number;
  text: string;
  level?: string;
  tag?: string;
  bad?: boolean;
}

type ConnectionStatus = "disconnected" | "connecting" | "connected";

export function App() {
  const [connectionStatus, setConnectionStatus] = useState<ConnectionStatus>("disconnected");
  const [snapshot, setSnapshot] = useState<SnapshotPayload | null>(null);
  const [currentNodeId, setCurrentNodeId] = useState<number | null>(null);
  const [lastGps, setLastGps] = useState(false);
  const [lastMock, setLastMock] = useState(false);
  const [nodeNames, setNodeNames] = useState<Record<string, string>>(() => loadNodeNames());
  const [discovered, setDiscovered] = useState<Set<number>>(() => new Set());
  const [observations, setObservations] = useState<Map<string, Observation>>(() => new Map());
  const [serialLog, setSerialLog] = useState<SerialLogEntry[]>([]);
  const [firmwareBuild, setFirmwareBuild] = useState("");
  const [debugEnabled, setDebugEnabled] = useState(false);
  const [nameDraft, setNameDraft] = useState("");
  const [nodeIdDraft, setNodeIdDraft] = useState("");
  const [altitudeDraft, setAltitudeDraft] = useState("");
  const [now, setNow] = useState(() => Date.now());

  const portRef = useRef<SerialPort | null>(null);
  const readerRef = useRef<ReadableStreamDefaultReader<Uint8Array> | null>(null);
  const writerRef = useRef<WritableStreamDefaultWriter<Uint8Array> | null>(null);
  const keepReadingRef = useRef(false);
  const logSeqRef = useRef(0);

  const connected = connectionStatus === "connected";
  const serialSupported = typeof navigator !== "undefined" && !!navigator.serial;

  useEffect(() => {
    const interval = window.setInterval(() => setNow(Date.now()), 1000);
    return () => window.clearInterval(interval);
  }, []);

  useEffect(() => {
    saveNodeNames(nodeNames);
  }, [nodeNames]);

  const labelFor = useCallback(
    (id: number) => nodeNames[String(id)] || `node-${id}`,
    [nodeNames],
  );

  const discoveredIds = useMemo(
    () => Array.from(discovered).sort((a, b) => a - b),
    [discovered],
  );

  const peerById = useMemo(() => {
    const peers = new Map<number, SnapshotPeer>();
    for (const peer of snapshot?.peers ?? []) {
      if (isNodeId(peer.id)) peers.set(peer.id, peer);
    }
    return peers;
  }, [snapshot]);

  const addLog = useCallback((entry: Omit<SerialLogEntry, "id">) => {
    setSerialLog((current) => {
      const next = [...current, { ...entry, id: ++logSeqRef.current }];
      return next.slice(-400);
    });
  }, []);

  const discoverNodes = useCallback((ids: Array<number | null | undefined>) => {
    setDiscovered((current) => {
      let changed = false;
      const next = new Set(current);
      for (const id of ids) {
        if (isNodeId(id) && !next.has(id)) {
          next.add(id);
          changed = true;
        }
      }
      return changed ? next : current;
    });
  }, []);

  const rememberNodeName = useCallback(
    (id: number | null | undefined, name: unknown) => {
      if (!isNodeId(id)) return;
      const clean = cleanName(name);
      if (!clean) return;
      discoverNodes([id]);
      setNodeNames((current) => (current[String(id)] === clean ? current : { ...current, [id]: clean }));
    },
    [discoverNodes],
  );

  const recordObservation = useCallback(
    (input: {
      fromId: number;
      toId: number;
      requestId?: number | null;
      rangeMm?: number | null;
      rangeSigmaMm?: number | null;
      rssi?: number | null;
      snr?: number | null;
      failReason?: string | null;
      source?: string;
      state?: Observation["state"];
      note?: string;
      discover?: boolean;
    }) => {
      if (!isNodeId(input.fromId) || !isNodeId(input.toId)) return;
      if (input.discover !== false || input.state === "ok") {
        discoverNodes([input.fromId, input.toId]);
      }
      const [aId, bId] = orderedPair(input.fromId, input.toId);
      setObservations((current) => {
        const next = new Map(current);
        next.set(pairKey(input.fromId, input.toId), {
          aId,
          bId,
          fromId: input.fromId,
          toId: input.toId,
          requestId: input.requestId ?? null,
          rangeMm: input.rangeMm ?? null,
          rangeSigmaMm: input.rangeSigmaMm ?? null,
          rssi: input.rssi ?? null,
          snr: input.snr ?? null,
          failReason: input.failReason ?? null,
          source: input.source ?? "",
          state: input.state ?? "unknown",
          note: input.note ?? input.source ?? "",
          updatedAt: Date.now(),
        });
        return next;
      });
    },
    [discoverNodes],
  );

  const ingestSnapshot = useCallback(
    (nextSnapshot: SnapshotPayload) => {
      setSnapshot(nextSnapshot);
      const nodeId = Number(nextSnapshot.node?.id);
      if (isNodeId(nodeId)) {
        setCurrentNodeId(nodeId);
        discoverNodes([nodeId]);
        setNodeIdDraft(String(nodeId));
      }
      rememberNodeName(isNodeId(nodeId) ? nodeId : null, nextSnapshot.node?.name);
      setLastGps(!!nextSnapshot.node?.gps);
      setLastMock(!!nextSnapshot.node?.mock);
      if (document.activeElement?.id !== "nameInput") {
        setNameDraft(typeof nextSnapshot.node?.name === "string" ? nextSnapshot.node.name : "");
      }

      for (const peer of nextSnapshot.peers ?? []) {
        if (!isNodeId(peer.id)) continue;
        discoverNodes([peer.id]);
        if (peer.range_valid) {
          recordObservation({
            fromId: nodeId,
            toId: peer.id,
            rangeMm: peer.range_mm,
            rssi: peer.rssi,
            snr: peer.snr,
            state: "ok",
            note: "snapshot",
            source: "snapshot",
          });
        } else if (!observations.has(pairKey(nodeId, peer.id))) {
          recordObservation({
            fromId: nodeId,
            toId: peer.id,
            state: "stale",
            note: "snapshot stale",
            source: "snapshot",
          });
        }
      }
    },
    [discoverNodes, observations, recordObservation, rememberNodeName],
  );

  const ingestRange = useCallback(
    (range: RangeRecord) => {
      const note = range.ok
        ? range.source || "range ok"
        : [range.source || "range failed", range.range_fail_reason].filter(Boolean).join("; ");
      recordObservation({
        fromId: range.from_id,
        toId: range.to_id,
        requestId: range.request_id,
        rangeMm: range.range_mm,
        rangeSigmaMm: range.range_sigma_mm,
        rssi: range.rssi_dbm,
        snr: range.snr_db,
        failReason: range.range_fail_reason,
        source: range.source ?? "log",
        state: range.ok ? "ok" : "fail",
        note,
      });
    },
    [recordObservation],
  );

  const ingestRecord = useCallback(
    (record: CaptureDataRecord, rangeFromText?: RangeRecord) => {
      if (record.type === "snapshot") {
        ingestSnapshot(record.data);
        return;
      }
      if (record.type === "range") {
        ingestRange(record);
        return;
      }
      if (record.type === "node_quality") {
        ingestNodeQuality(record, discoverNodes);
        return;
      }
      if (record.type === "log") {
        addLog({ text: record.text, level: record.level, tag: record.tag, bad: record.level === "ERROR" });
        const build = parseFirmwareBuild(record);
        if (build) setFirmwareBuild(build);
        if (rangeFromText) ingestRange(rangeFromText);
      }
    },
    [addLog, discoverNodes, ingestRange, ingestSnapshot],
  );

  const handleLine = useCallback(
    (line: string) => {
      const parsed = parseInboundLine(line, Date.now());
      if (!parsed) return;
      ingestRecord(parsed.record, parsed.rangeFromText);
      if (parsed.source === "unknown-json") {
        addLog({ text: `unknown JSON: ${line}`, bad: true });
      }
    },
    [addLog, ingestRecord],
  );

  const sendCommand = useCallback(async (obj: Record<string, unknown>) => {
    const writer = writerRef.current;
    if (!writer) return;
    await writer.write(new TextEncoder().encode(`${JSON.stringify(obj)}\n`));
  }, []);

  const connect = useCallback(async () => {
    if (!navigator.serial || connectionStatus !== "disconnected") return;
    setConnectionStatus("connecting");
    try {
      const port = await navigator.serial.requestPort();
      await port.open({ baudRate: BAUD_RATE });
      portRef.current = port;
      writerRef.current = port.writable?.getWriter() ?? null;
      keepReadingRef.current = true;
      setConnectionStatus("connected");
      addLog({ text: `[connected @ ${BAUD_RATE} baud]` });
      void readLoop(port, keepReadingRef, readerRef, handleLine, addLog);
      await sendCommand({ cmd: "get" });
    } catch (error) {
      setConnectionStatus("disconnected");
      addLog({ text: `connect failed: ${(error as Error).message}`, bad: true });
    }
  }, [addLog, connectionStatus, handleLine, sendCommand]);

  const disconnect = useCallback(async () => {
    if (writerRef.current && debugEnabled) {
      try {
        await sendCommand({ cmd: "debug", on: false });
      } catch {
        // Continue disconnect even if the node is already gone.
      }
    }
    keepReadingRef.current = false;
    try {
      await readerRef.current?.cancel();
    } catch {
      // Ignore cancellation races during disconnect.
    }
    try {
      writerRef.current?.releaseLock();
    } catch {
      // Ignore stale writer locks.
    }
    try {
      await portRef.current?.close();
    } catch {
      // Ignore already-closed ports.
    }
    readerRef.current = null;
    writerRef.current = null;
    portRef.current = null;
    setDebugEnabled(false);
    setConnectionStatus("disconnected");
    addLog({ text: "[disconnected]" });
  }, [addLog, debugEnabled, sendCommand]);

  useEffect(() => {
    const beforeUnload = () => {
      if (portRef.current) void disconnect();
    };
    window.addEventListener("beforeunload", beforeUnload);
    return () => window.removeEventListener("beforeunload", beforeUnload);
  }, [disconnect]);

  const nodeRows = useMemo(
    () => discoveredIds.map((id) => ({ id, name: nodeNames[String(id)] || `node-${id}` })),
    [discoveredIds, nodeNames],
  );

  const distanceRows = useMemo(() => {
    const rows = new Map(observations);
    for (let i = 0; i < discoveredIds.length; i++) {
      for (let j = i + 1; j < discoveredIds.length; j++) {
        const key = pairKey(discoveredIds[i], discoveredIds[j]);
        if (!rows.has(key)) {
          rows.set(key, {
            aId: discoveredIds[i],
            bId: discoveredIds[j],
            fromId: null,
            toId: null,
            requestId: null,
            rangeMm: null,
            rangeSigmaMm: null,
            rssi: null,
            snr: null,
            failReason: null,
            source: "",
            state: "missing",
            note: "no observation",
            updatedAt: null,
          });
        }
      }
    }
    return Array.from(rows.values()).sort((a, b) => a.aId - b.aId || a.bId - b.bId);
  }, [discoveredIds, observations]);

  const saveCurrentName = () => {
    const clean = cleanName(nameDraft);
    if (!clean) return;
    if (isNodeId(currentNodeId)) rememberNodeName(currentNodeId, clean);
    void sendCommand({ cmd: "name", value: clean });
  };

  const setNodeId = () => {
    const id = Number.parseInt(nodeIdDraft, 10);
    if (id >= 0 && id <= 3) void sendCommand({ cmd: "node_id", id });
  };

  const setAltitude = () => {
    const altMm = Number.parseInt(altitudeDraft, 10);
    if (Number.isFinite(altMm)) void sendCommand({ cmd: "alt", alt_mm: altMm });
  };

  const toggleDebug = () => {
    const next = !debugEnabled;
    setDebugEnabled(next);
    void sendCommand({ cmd: "debug", on: next }).catch((error) => {
      setDebugEnabled(!next);
      addLog({ text: `debug command failed: ${(error as Error).message}`, bad: true });
    });
  };

  return (
    <main className="app-shell">
      <header className="topbar">
        <div className="brand">
          <Cable size={18} aria-hidden="true" />
          <h1>nav-mcu telemetry</h1>
        </div>
        <span className={`status-pill ${connected ? "status-on" : "status-off"}`}>
          {connectionStatus}
        </span>
        <div className="topbar-actions">
          <button className="primary" onClick={connect} disabled={!serialSupported || connectionStatus !== "disconnected"}>
            <PlugZap size={16} aria-hidden="true" />
            Connect
          </button>
          <button onClick={disconnect} disabled={!connected}>
            <Unplug size={16} aria-hidden="true" />
            Disconnect
          </button>
          <button className={debugEnabled ? "toggle-active" : ""} onClick={toggleDebug} disabled={!connected}>
            <Bug size={16} aria-hidden="true" />
            Debug
          </button>
        </div>
      </header>

      {!serialSupported ? (
        <div className="support-warning">Web Serial is unavailable in this browser.</div>
      ) : null}

      <div className="content-grid">
        <aside className="side-column">
          <section className="panel">
            <h2>This Node</h2>
            <KeyValue label="Name" value={snapshot?.node?.name || "(unnamed)"} />
            <KeyValue label="Node ID" value={snapshot?.node?.id ?? DASH} />
            <KeyValue label="Mode" value={snapshot?.mode ?? DASH} />
            <KeyValue label="Solution" value={snapshot?.sol ?? DASH} />
            <KeyValue label="Source" value={snapshot?.src ?? DASH} />
            <KeyValue label="Reject" value={snapshot?.reject ?? DASH} />
            <KeyValue label="Position" value={formatPosition(snapshot)} />
            <KeyValue label="GPS" value={lastGps ? "on (local GNSS)" : "off (trilateration)"} />
            <KeyValue label="Mock peers" value={lastMock ? "on" : "off"} />

            <div className="form-row">
              <input
                id="nameInput"
                value={nameDraft}
                onChange={(event) => setNameDraft(event.target.value)}
                placeholder="node name"
                maxLength={23}
              />
              <button onClick={saveCurrentName} disabled={!connected}>
                <Save size={15} aria-hidden="true" />
                Save
              </button>
            </div>
            <div className="form-row">
              <input
                value={nodeIdDraft}
                onChange={(event) => setNodeIdDraft(event.target.value)}
                type="number"
                min={0}
                max={3}
                placeholder="node id"
              />
              <button onClick={setNodeId} disabled={!connected}>
                <HardDrive size={15} aria-hidden="true" />
                Set ID
              </button>
            </div>
            <div className="form-row two">
              <button onClick={() => void sendCommand({ cmd: "gps", enabled: !lastGps })} disabled={!connected}>
                <Satellite size={15} aria-hidden="true" />
                GPS
              </button>
              <button onClick={() => void sendCommand({ cmd: "mock", enabled: !lastMock })} disabled={!connected}>
                <FlaskConical size={15} aria-hidden="true" />
                Mock
              </button>
            </div>
            <div className="form-row">
              <input
                value={altitudeDraft}
                onChange={(event) => setAltitudeDraft(event.target.value)}
                type="number"
                placeholder="altitude mm"
              />
              <button onClick={setAltitude} disabled={!connected}>
                <RotateCcw size={15} aria-hidden="true" />
                Altitude
              </button>
            </div>
          </section>

          <section className="panel">
            <h2>Node Names</h2>
            <table className="compact-table">
              <thead>
                <tr>
                  <th>ID</th>
                  <th>Name</th>
                  <th>NVS</th>
                </tr>
              </thead>
              <tbody>
                {nodeRows.length === 0 ? (
                  <tr>
                    <td colSpan={3} className="empty-cell">
                      No discovered nodes.
                    </td>
                  </tr>
                ) : (
                  nodeRows.map((row) => (
                    <tr key={row.id}>
                      <td className="mono">#{row.id}</td>
                      <td>
                        <input
                          value={row.name}
                          maxLength={23}
                          onChange={(event) =>
                            setNodeNames((current) => ({ ...current, [row.id]: cleanName(event.target.value) }))
                          }
                        />
                      </td>
                      <td>
                        {row.id === currentNodeId ? (
                          <button
                            className="small-button"
                            onClick={() => {
                              const name = cleanName(nodeNames[String(row.id)]);
                              if (name) void sendCommand({ cmd: "name", value: name });
                            }}
                            disabled={!connected}
                          >
                            Save
                          </button>
                        ) : (
                          <span className="muted">connect</span>
                        )}
                      </td>
                    </tr>
                  ))
                )}
              </tbody>
            </table>
          </section>
        </aside>

        <section className="main-column">
          <section className="panel">
            <h2>Distance Observations</h2>
            <table className="data-table distance-table">
              <thead>
                <tr>
                  <th>Pair</th>
                  <th>State</th>
                  <th>Distance</th>
                  <th>Request</th>
                  <th>Age</th>
                  <th>RSSI</th>
                  <th>SNR</th>
                </tr>
              </thead>
              <tbody>
                {distanceRows.length === 0 ? (
                  <tr>
                    <td colSpan={7} className="empty-cell">
                      No range observations yet.
                    </td>
                  </tr>
                ) : (
                  distanceRows.map((obs) => {
                    const ageMs = obs.updatedAt == null ? null : now - obs.updatedAt;
                    const displayState = ageMs != null && ageMs > 5000 && obs.state === "ok" ? "stale" : obs.state;
                    return (
                      <tr key={`${obs.aId}:${obs.bId}`} className={displayState === "ok" ? "" : "stale-row"}>
                        <td data-label="Pair" className="text-cell">
                          {pairLabel(obs.aId, obs.bId, labelFor)}
                        </td>
                        <td data-label="State" className={stateClass(displayState)}>
                          <span title={obs.note}>{displayState}</span>
                        </td>
                        <td data-label="Distance" className={`mono num ${displayState === "ok" ? "ok" : ""}`}>
                          {distanceText(obs)}
                        </td>
                        <td data-label="Request" className="mono num">
                          {obs.requestId ?? DASH}
                        </td>
                        <td data-label="Age" className="mono num">
                          {ageMs == null ? DASH : formatAge(ageMs)}
                        </td>
                        <td data-label="RSSI" className="mono num">
                          {formatSigned(obs.rssi, 1)}
                        </td>
                        <td data-label="SNR" className="mono num">
                          {formatSigned(obs.snr, 1)}
                        </td>
                      </tr>
                    );
                  })
                )}
              </tbody>
            </table>
          </section>

          <section className="panel">
            <h2>Peer Snapshot</h2>
            <table className="data-table peer-table">
              <thead>
                <tr>
                  <th>Node</th>
                  <th>Distance</th>
                  <th>RSSI</th>
                  <th>SNR</th>
                  <th>Qual</th>
                  <th>GNSS</th>
                  <th>Latitude</th>
                  <th>Longitude</th>
                  <th>Alt</th>
                </tr>
              </thead>
              <tbody>
                {discoveredIds.length === 0 ? (
                  <tr>
                    <td colSpan={9} className="empty-cell">
                      No discovered nodes yet.
                    </td>
                  </tr>
                ) : (
                  discoveredIds.map((id) => {
                    const peer = peerById.get(id);
                    const obs = currentNodeId == null || id === currentNodeId ? null : observations.get(pairKey(currentNodeId, id));
                    const obsFresh = !!obs?.updatedAt && obs.state === "ok" && now - obs.updatedAt <= 5000;
                    const rangeMm = peer?.range_valid ? peer.range_mm : obs?.rangeMm;
                    const hasGnss = !!peer?.gnss;
                    return (
                      <tr key={id} className={id !== currentNodeId && !obsFresh ? "stale-row" : ""}>
                        <td data-label="Node" className="text-cell">
                          {labelFor(id)} #{id}
                          {id === currentNodeId ? " (this)" : ""}
                        </td>
                        <td data-label="Distance" className="mono num">
                          {id === currentNodeId ? DASH : formatMetersFromMm(rangeMm)}
                        </td>
                        <td data-label="RSSI" className="mono num">
                          {id === currentNodeId ? DASH : formatSigned(obs?.rssi ?? peer?.rssi, 1)}
                        </td>
                        <td data-label="SNR" className="mono num">
                          {id === currentNodeId ? DASH : formatSigned(obs?.snr ?? peer?.snr, 1)}
                        </td>
                        <td data-label="Qual" className="mono num">
                          {hasGnss ? formatQuality(peer?.quality) : DASH}
                        </td>
                        <td data-label="GNSS">{hasGnss ? "yes" : DASH}</td>
                        <td data-label="Latitude" className="mono num">
                          {hasGnss ? formatDeg(peer?.lat_e7) : DASH}
                        </td>
                        <td data-label="Longitude" className="mono num">
                          {hasGnss ? formatDeg(peer?.lon_e7) : DASH}
                        </td>
                        <td data-label="Alt" className="mono num">
                          {hasGnss ? formatMetersFromMm(peer?.alt_mm) : DASH}
                        </td>
                      </tr>
                    );
                  })
                )}
              </tbody>
            </table>
          </section>

          <section className="panel">
            <div className="section-title-row">
              <h2>Serial Log</h2>
              {firmwareBuild ? <span className="meta-chip">Build {firmwareBuild}</span> : null}
            </div>
            <div className="serial-log" aria-live="polite">
              {serialLog.map((entry) => (
                <div key={entry.id} className={entry.bad ? "bad" : ""}>
                  {entry.text}
                </div>
              ))}
            </div>
          </section>
        </section>
      </div>
    </main>
  );
}

function KeyValue({ label, value }: { label: string; value: React.ReactNode }) {
  return (
    <div className="kv">
      <span>{label}</span>
      <span className="mono">{value}</span>
    </div>
  );
}

async function readLoop(
  port: SerialPort,
  keepReadingRef: React.MutableRefObject<boolean>,
  readerRef: React.MutableRefObject<ReadableStreamDefaultReader<Uint8Array> | null>,
  handleLine: (line: string) => void,
  addLog: (entry: Omit<SerialLogEntry, "id">) => void,
) {
  const decoder = new TextDecoder();
  let buffer = "";
  while (keepReadingRef.current && port.readable) {
    const reader = port.readable.getReader();
    readerRef.current = reader;
    try {
      while (true) {
        const { value, done } = await reader.read();
        if (done) break;
        buffer += decoder.decode(value, { stream: true });
        let newline = buffer.indexOf("\n");
        while (newline >= 0) {
          const line = buffer.slice(0, newline).trim();
          buffer = buffer.slice(newline + 1);
          if (line) handleLine(line);
          newline = buffer.indexOf("\n");
        }
      }
    } catch (error) {
      if (keepReadingRef.current) {
        addLog({ text: `read error: ${(error as Error).message}`, bad: true });
      }
    } finally {
      reader.releaseLock();
      if (readerRef.current === reader) readerRef.current = null;
    }
  }
}

function ingestNodeQuality(record: NodeQualityRecord, discoverNodes: (ids: Array<number | null | undefined>) => void) {
  discoverNodes([record.node_id, ...(record.data.anchor_ids ?? [])]);
}

function loadNodeNames(): Record<string, string> {
  try {
    const parsed = JSON.parse(localStorage.getItem(NODE_NAMES_KEY) || "{}") as unknown;
    return parsed && typeof parsed === "object" && !Array.isArray(parsed) ? (parsed as Record<string, string>) : {};
  } catch {
    return {};
  }
}

function saveNodeNames(names: Record<string, string>) {
  try {
    localStorage.setItem(NODE_NAMES_KEY, JSON.stringify(names));
  } catch {
    // Ignore private-mode storage failures.
  }
}

function cleanName(value: unknown): string {
  return String(value ?? "").trim().slice(0, 23);
}

function orderedPair(aId: number, bId: number): [number, number] {
  return aId <= bId ? [aId, bId] : [bId, aId];
}

function pairKey(aId: number, bId: number): string {
  const [left, right] = orderedPair(aId, bId);
  return `${left}:${right}`;
}

function pairLabel(aId: number, bId: number, labelFor: (id: number) => string): string {
  const [left, right] = orderedPair(aId, bId);
  return `${labelFor(left)} #${left} <-> ${labelFor(right)} #${right}`;
}

function hasNumber(value: unknown): value is number {
  return typeof value === "number" && Number.isFinite(value);
}

function formatDeg(e7: unknown): string {
  return hasNumber(e7) ? (e7 / 1e7).toFixed(6) : DASH;
}

function formatMetersFromMm(mm: unknown): string {
  return hasNumber(mm) ? `${(mm / 1000).toFixed(2)} m` : DASH;
}

function formatSigned(value: unknown, digits = 0): string {
  return hasNumber(value) ? value.toFixed(digits) : DASH;
}

function formatQuality(value: unknown): string {
  return hasNumber(value) ? value.toFixed(2) : DASH;
}

function formatAge(ms: number): string {
  return ms < 1000 ? "now" : `${Math.floor(ms / 1000)}s`;
}

function formatPosition(snapshot: SnapshotPayload | null): string {
  if (!snapshot?.pos || !snapshot.src || snapshot.src === "NONE" || snapshot.sol === "NONE" || snapshot.sol === "REJECTED") {
    return DASH;
  }
  return `${formatDeg(snapshot.pos.lat_e7)}, ${formatDeg(snapshot.pos.lon_e7)} @ ${formatMetersFromMm(snapshot.pos.alt_mm)}`;
}

function distanceText(obs: Observation): string {
  if (obs.state === "ok" && hasNumber(obs.rangeMm)) return formatMetersFromMm(obs.rangeMm);
  return DASH;
}

function stateClass(state: Observation["state"] | "stale"): string {
  if (state === "ok") return "ok";
  if (state === "stale" || state === "missing") return "warn";
  return "bad";
}
