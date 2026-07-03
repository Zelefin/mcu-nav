export const CAPTURE_SCHEMA_VERSION = 1;
export const APP_VERSION = "react-0.1.0";

export type JsonObject = Record<string, unknown>;

export interface SnapshotPeer {
  id?: number;
  gnss?: boolean;
  lat_e7?: number;
  lon_e7?: number;
  alt_mm?: number;
  range_mm?: number;
  range_valid?: boolean;
  rssi?: number;
  snr?: number;
  quality?: number;
}

export interface SnapshotPayload extends JsonObject {
  t?: number;
  node?: {
    id?: number;
    name?: string;
    gps?: boolean;
    mock?: boolean;
  };
  mode?: string;
  sol?: string;
  src?: string;
  reject?: string;
  pos?: {
    lat_e7?: number;
    lon_e7?: number;
    alt_mm?: number;
  };
  num_anchors?: number;
  peers?: SnapshotPeer[];
}

export interface MetaRecord {
  type: "meta";
  ts_ms: number;
  schema_version: number;
  app_version: string;
  firmware_build: string;
  node_id: number;
  node_name: string;
  debug: boolean;
}

export interface SnapshotRecord extends JsonObject {
  type: "snapshot";
  ts_ms?: number;
  ts?: number;
  data: SnapshotPayload;
}

export interface NodeQualityData extends JsonObject {
  nav_mode?: string;
  solution_status?: string;
  solution_source?: string;
  residual_rms_m?: number;
  max_residual_m?: number;
  geometry_score?: number;
  total_quality?: number;
  num_anchors?: number;
  anchor_ids?: number[];
  fix_type?: string;
  satellites?: number;
  hdop_centi?: number;
  hacc_mm?: number;
  vacc_mm?: number;
  lat_e7?: number;
  lon_e7?: number;
  alt_mm?: number;
  packet_seq?: number;
}

export interface NodeQualityRecord extends JsonObject {
  type: "node_quality";
  ts_ms?: number;
  ts?: number;
  node_id: number;
  origin: "local" | "peer" | string;
  age_ms?: number;
  data: NodeQualityData;
}

export interface RangeRecord extends JsonObject {
  type: "range";
  ts_ms?: number;
  ts?: number;
  from_id: number;
  to_id: number;
  request_id?: number;
  ok: boolean;
  range_mm?: number;
  range_sigma_mm?: number;
  rssi_dbm?: number;
  snr_db?: number;
  range_fail_reason?: string;
  source?: "log" | "air_report" | "snapshot" | string;
}

export interface LogRecord extends JsonObject {
  type: "log";
  ts_ms?: number;
  ts?: number;
  level?: string;
  tag?: string;
  text: string;
}

export type CaptureDataRecord = SnapshotRecord | NodeQualityRecord | RangeRecord | LogRecord;
export type CaptureRecord = MetaRecord | CaptureDataRecord;

export interface ParsedInboundLine {
  line: string;
  source: "typed" | "legacy-snapshot" | "legacy-log" | "unknown-json";
  record: CaptureDataRecord;
  rangeFromText?: RangeRecord;
}

export function buildMetaRecord(input: {
  tsMs: number;
  firmwareBuild?: string;
  nodeId: number;
  nodeName?: string;
  debug: boolean;
}): MetaRecord {
  return {
    type: "meta",
    ts_ms: input.tsMs,
    schema_version: CAPTURE_SCHEMA_VERSION,
    app_version: APP_VERSION,
    firmware_build: input.firmwareBuild ?? "",
    node_id: input.nodeId,
    node_name: input.nodeName ?? "",
    debug: input.debug,
  };
}

export function withBrowserTimestamp<T extends CaptureDataRecord>(record: T, tsMs: number): T & { ts_ms: number } {
  return { ...record, ts_ms: tsMs };
}

export function parseInboundLine(line: string, tsMs = Date.now()): ParsedInboundLine | null {
  const trimmed = line.trim();
  if (!trimmed) {
    return null;
  }

  if (trimmed.startsWith("{")) {
    try {
      const parsed = JSON.parse(trimmed) as unknown;
      if (isJsonObject(parsed)) {
        const typed = normalizeTypedRecord(parsed, tsMs);
        if (typed) {
          const rangeFromText = typed.type === "log" ? parseRangeLogText(typed.text, typed.ts, tsMs) : undefined;
          return { line: trimmed, source: "typed", record: typed, rangeFromText };
        }
        if (isSnapshotPayload(parsed)) {
          return {
            line: trimmed,
            source: "legacy-snapshot",
            record: { type: "snapshot", ts_ms: tsMs, ts: numberOrUndefined(parsed.t), data: parsed },
          };
        }
        return {
          line: trimmed,
          source: "unknown-json",
          record: makeLogRecord(trimmed, tsMs),
        };
      }
    } catch {
      // Fall through and preserve the line as a log record.
    }
  }

  const log = makeLogRecord(trimmed, tsMs);
  return {
    line: trimmed,
    source: "legacy-log",
    record: log,
    rangeFromText: parseRangeLogText(trimmed, log.ts, tsMs),
  };
}

export function parseNdjsonLine(line: string): CaptureRecord | null {
  const trimmed = line.trim();
  if (!trimmed) return null;
  const parsed = JSON.parse(trimmed) as unknown;
  if (!isJsonObject(parsed) || typeof parsed.type !== "string") return null;
  if (parsed.type === "meta") return parsed as unknown as MetaRecord;
  const data = normalizeTypedRecord(parsed, numberOrUndefined(parsed.ts_ms) ?? Date.now());
  return data;
}

export function validateCaptureLines(text: string): { ok: boolean; errors: string[]; records: CaptureRecord[] } {
  const errors: string[] = [];
  const records: CaptureRecord[] = [];
  const lines = text.split(/\r?\n/).filter((line) => line.trim().length > 0);
  if (lines.length === 0) {
    return { ok: false, errors: ["capture is empty"], records };
  }

  lines.forEach((line, index) => {
    try {
      const record = parseNdjsonLine(line);
      if (!record) {
        errors.push(`line ${index + 1}: not a capture object`);
        return;
      }
      records.push(record);
      if (index === 0 && record.type !== "meta") {
        errors.push("line 1: first record must be meta for browser captures");
      }
      if (index > 0 && record.type === "meta") {
        errors.push(`line ${index + 1}: meta record after first line`);
      }
      if (record.type !== "meta" && typeof record.ts_ms !== "number") {
        errors.push(`line ${index + 1}: missing browser ts_ms`);
      }
    } catch (error) {
      errors.push(`line ${index + 1}: ${(error as Error).message}`);
    }
  });

  return { ok: errors.length === 0, errors, records };
}

function normalizeTypedRecord(obj: JsonObject, tsMs: number): CaptureDataRecord | null {
  if (obj.type === "snapshot") {
    const data = isJsonObject(obj.data) ? (obj.data as SnapshotPayload) : undefined;
    if (!data) return null;
    return { ...obj, type: "snapshot", ts_ms: numberOrUndefined(obj.ts_ms), ts: recordTs(obj, data.t), data };
  }
  if (obj.type === "node_quality") {
    const data = isJsonObject(obj.data) ? (obj.data as NodeQualityData) : {};
    const nodeId = numberOrUndefined(obj.node_id);
    if (nodeId === undefined) return null;
    return {
      ...obj,
      type: "node_quality",
      ts_ms: numberOrUndefined(obj.ts_ms),
      ts: recordTs(obj),
      node_id: nodeId,
      origin: typeof obj.origin === "string" ? obj.origin : "peer",
      age_ms: numberOrUndefined(obj.age_ms),
      data,
    };
  }
  if (obj.type === "range") {
    const fromId = numberOrUndefined(obj.from_id);
    const toId = numberOrUndefined(obj.to_id);
    if (fromId === undefined || toId === undefined || typeof obj.ok !== "boolean") return null;
    return {
      ...obj,
      type: "range",
      ts_ms: numberOrUndefined(obj.ts_ms),
      ts: recordTs(obj),
      from_id: fromId,
      to_id: toId,
      request_id: numberOrUndefined(obj.request_id),
      ok: obj.ok,
      range_mm: numberOrUndefined(obj.range_mm),
      range_sigma_mm: numberOrUndefined(obj.range_sigma_mm),
      rssi_dbm: numberOrUndefined(obj.rssi_dbm),
      snr_db: numberOrUndefined(obj.snr_db),
      range_fail_reason: typeof obj.range_fail_reason === "string" ? obj.range_fail_reason : undefined,
      source: typeof obj.source === "string" ? obj.source : "log",
    };
  }
  if (obj.type === "log") {
    const text = typeof obj.text === "string" ? obj.text : JSON.stringify(obj);
    return {
      ...obj,
      type: "log",
      ts_ms: numberOrUndefined(obj.ts_ms),
      ts: recordTs(obj) ?? parseLogTimestamp(text),
      level: typeof obj.level === "string" ? obj.level : parseLogFields(text).level,
      tag: typeof obj.tag === "string" ? obj.tag : parseLogFields(text).tag,
      text,
    };
  }
  if (typeof obj.type === "string") {
    return makeLogRecord(JSON.stringify(obj), tsMs);
  }
  return null;
}

function makeLogRecord(text: string, tsMs: number): LogRecord {
  const parsed = parseLogFields(text);
  return {
    type: "log",
    ts_ms: tsMs,
    ts: parsed.ts,
    level: parsed.level,
    tag: parsed.tag,
    text,
  };
}

function parseLogFields(text: string): { ts?: number; level?: string; tag?: string } {
  const match = text.match(/t=(\d+)ms\s+\[([A-Z]+)]\s+\[([^\]]+)]/);
  if (!match) return {};
  return {
    ts: Number(match[1]),
    level: match[2],
    tag: match[3],
  };
}

function parseLogTimestamp(text: string): number | undefined {
  return parseLogFields(text).ts;
}

export function parseFirmwareBuild(record: LogRecord): string | null {
  const match = record.text.match(/\bBuild:\s*(.+)$/);
  return match ? match[1].trim() : null;
}

function parseRangeLogText(text: string, fallbackTs: number | undefined, tsMs: number): RangeRecord | undefined {
  if (!text.includes("range_result ok=")) return undefined;
  const fromId = intField(text, "from");
  const toId = intField(text, "to");
  if (!isNodeId(fromId) || !isNodeId(toId)) return undefined;
  const ok = fieldValue(text, "ok") === "true";
  const failReason = fieldValue(text, "range_fail_reason");
  return {
    type: "range",
    ts_ms: tsMs,
    ts: fallbackTs ?? parseLogTimestamp(text),
    from_id: fromId,
    to_id: toId,
    request_id: intField(text, "request_id") ?? undefined,
    ok,
    range_mm: intField(text, "range_mm") ?? undefined,
    range_sigma_mm: intField(text, "range_sigma_mm") ?? undefined,
    rssi_dbm: floatField(text, "rssi_dbm") ?? undefined,
    snr_db: floatField(text, "snr_db") ?? undefined,
    range_fail_reason: ok ? undefined : (failReason ?? "UNKNOWN"),
    source: fieldValue(text, "source") ?? "log",
  };
}

export function isNodeId(id: unknown): id is number {
  return Number.isInteger(id) && Number(id) >= 0 && Number(id) < 256;
}

function isJsonObject(value: unknown): value is JsonObject {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

function isSnapshotPayload(value: JsonObject): value is SnapshotPayload {
  return isJsonObject(value.node) && Array.isArray(value.peers);
}

function recordTs(obj: JsonObject, fallback?: unknown): number | undefined {
  return numberOrUndefined(obj.ts) ?? numberOrUndefined(fallback);
}

export function numberOrUndefined(value: unknown): number | undefined {
  if (typeof value === "number" && Number.isFinite(value)) return value;
  return undefined;
}

function fieldValue(line: string, key: string): string | null {
  const match = line.match(new RegExp(`${key}=("[^"]*"|\\S+)`));
  if (!match) return null;
  const raw = match[1];
  return raw.startsWith('"') && raw.endsWith('"') ? raw.slice(1, -1) : raw;
}

function intField(line: string, key: string): number | null {
  const value = fieldValue(line, key);
  if (value == null) return null;
  const parsed = Number.parseInt(value, 10);
  return Number.isNaN(parsed) ? null : parsed;
}

function floatField(line: string, key: string): number | null {
  const value = fieldValue(line, key);
  if (value == null) return null;
  const parsed = Number.parseFloat(value);
  return Number.isNaN(parsed) ? null : parsed;
}
