const { Plugin } = require("@lumiastream/plugin");
const http = require("http");

const MATRIX_EVENT_MIN = 0;
const MATRIX_EVENT_MAX = 71;
const PET_EVENT_BASE = 72;
const PET_EVENT_COUNT = 22;
const EVENT_MAX = PET_EVENT_BASE + PET_EVENT_COUNT - 1;
const KEY_COUNT = 36;
const SEQ_RESET_GAP_MS = 5000;

const PET_NAMES = [
  "hunger_up", "hunger_down",
  "happiness_up", "happiness_down",
  "energy_up", "energy_down",
  "hygiene_up", "hygiene_down",
  "health_up", "health_down",
  "discipline_up", "discipline_down",
  "poop_up", "poop_down",
  "stage_up", "stage_down",
  "sleep_on", "sleep_off",
  "sick_on", "sick_off",
  "alive_on", "alive_off",
];

const PET_FIELD_NAMES = {
  0: "hunger",
  1: "happiness",
  2: "energy",
  3: "hygiene",
  4: "health",
  5: "discipline",
  6: "poop",
  7: "stage",
  8: "sleeping",
  9: "sick",
  10: "alive",
};

// ---------------- helpers (no AbortController) ----------------
function withTimeout(promise, timeoutMs) {
  const ms = Number(timeoutMs) || 2000;
  return Promise.race([
    promise,
    new Promise((_, reject) => setTimeout(() => reject(new Error("Request timed out")), ms)),
  ]);
}

function normalizeBaseUrl(baseUrl) {
  const u = String(baseUrl || "").trim().replace(/\/+$/, "");
  if (!u) return "";
  if (!/^https?:\/\//i.test(u)) return "";
  return u;
}

function normalizePath(p) {
  const s = String(p || "").trim();
  if (!s) return "/";
  return s.startsWith("/") ? s : `/${s}`;
}

function joinUrl(base, path) {
  const b = String(base || "").replace(/\/+$/, "");
  const p = String(path || "");
  return `${b}${p.startsWith("/") ? p : `/${p}`}`;
}

async function httpGet(url, timeoutMs) {
  const res = await withTimeout(fetch(url, { method: "GET" }), timeoutMs);
  const body = await res.text().catch(() => "");
  if (!res.ok) throw new Error(`HTTP ${res.status}${body ? `: ${body}` : ""}`);
  return body;
}

async function httpPostJson(url, obj, timeoutMs) {
  const res = await withTimeout(
    fetch(url, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(obj),
    }),
    timeoutMs
  );
  const body = await res.text().catch(() => "");
  if (!res.ok) throw new Error(`HTTP ${res.status}${body ? `: ${body}` : ""}`);
  return body;
}

function safeNowMs() {
  return Date.now();
}

function msToAgeSeconds(ms) {
  return Math.max(0, Math.floor(ms / 1000));
}

function parseKeyLabels(text) {
  const raw = typeof text === "string" ? text : "";
  const lines = raw.split(/\r?\n/).map((l) => l.trim());
  const out = new Array(36).fill("");
  for (let i = 0; i < Math.min(36, lines.length); i++) out[i] = lines[i];
  return out;
}

function parseJsonSafe(text) {
  try {
    return JSON.parse(text);
  } catch {
    return null;
  }
}

module.exports = class LumiConBridgeIntegratedV110 extends Plugin {
  constructor(manifest, context) {
    super(manifest, context);

    this._server = null;
    this._lastSeqByDevice = new Map();

    this._deviceConnected = false;
    this._deviceId = "";
    this._deviceIp = "";
    this._lastRssi = 0;
    this._lastSeenMs = 0;
    this._lastStatusText = "";

    this._statusTimer = null;

    this._labelsShort = new Array(36).fill("");
    this._labelsLong = new Array(36).fill("");
    this._labelsFallback = new Array(36).fill("");

    this._lastToastMs = 0;
    this._lastVerboseToastMs = 0;
  }

  async onload() {
    this._refreshKeyLabels();
    await this.lumia.setVariable("device_connected", false);
    await this.lumia.setVariable("device_status_text", "OFFLINE");
    this._startStatusTimer();
    if (this._isEnabled()) await this._startServer();
  }

  async onunload() {
    this._stopStatusTimer();
    await this._stopServer();
  }

  async onsettingsupdate(settings, previousSettings) {
    this._refreshKeyLabels();
    const enabledChanged = Boolean(settings?.enabled) !== Boolean(previousSettings?.enabled);
    const portChanged = Number(settings?.listenPort) !== Number(previousSettings?.listenPort);
    if (enabledChanged || portChanged) {
      await this._stopServer();
      if (this._isEnabled()) await this._startServer();
    }
  }

  _isEnabled() {
    return Boolean(this.settings?.enabled ?? true);
  }

  _getPort() {
    const port = Number(this.settings?.listenPort ?? 8787);
    return Number.isInteger(port) && port > 0 && port <= 65535 ? port : 8787;
  }

  _getSecret() {
    return String(this.settings?.secret ?? "").trim();
  }

  _getOfflineTimeoutMs() {
    const sec = Number(this.settings?.offlineTimeoutSec ?? 30);
    const safeSec = Number.isFinite(sec) && sec >= 5 ? sec : 30;
    return Math.floor(safeSec * 1000);
  }

  _debugToastsEnabled() {
    return Boolean(this.settings?.debugToasts ?? false);
  }

  _toastVerbosity() {
    const v = String(this.settings?.toastVerbosity ?? "important");
    return v === "verbose" ? "verbose" : "important";
  }

  _refreshKeyLabels() {
    this._labelsShort = parseKeyLabels(this.settings?.keyLabelsShort);
    this._labelsLong = parseKeyLabels(this.settings?.keyLabelsLong);
    this._labelsFallback = parseKeyLabels(this.settings?.keyLabels);
  }

  _getKeyLabel(kind, keyIndex) {
    const idx = Number(keyIndex);
    if (!Number.isInteger(idx) || idx < 0 || idx >= 36) return "";
    const list = kind === "long" ? this._labelsLong : this._labelsShort;
    const picked = String(list[idx] || "").trim();
    if (picked) return picked;
    return String(this._labelsFallback[idx] || "").trim();
  }

  async _toast(message, timeMs = 2500, isVerbose = false) {
    if (!this._debugToastsEnabled()) return;
    const now = safeNowMs();
    if (now - this._lastToastMs < 800) return;
    if (isVerbose && now - this._lastVerboseToastMs < 250) return;
    this._lastToastMs = now;
    if (isVerbose) this._lastVerboseToastMs = now;
    try {
      await this.lumia.showToast({ message: String(message), time: timeMs });
    } catch {}
  }

  async _setDeviceConnected(isConnected) {
    if (this._deviceConnected === isConnected) return;
    this._deviceConnected = isConnected;
    await this.lumia.setVariable("device_connected", isConnected);
    try {
      await this.lumia.updateConnection(isConnected);
    } catch {}
    if (isConnected) await this._toast("Lumi-Con connected", 2500, false);
    else await this._toast("Lumi-Con offline", 2500, false);
  }

  _formatStatusText(nowMs) {
    if (!this._deviceConnected || !this._lastSeenMs) return "OFFLINE";
    const ageSec = msToAgeSeconds(nowMs - this._lastSeenMs);
    const rssi = Number(this._lastRssi);
    const rssiPart = Number.isFinite(rssi) && rssi !== 0 ? `RSSI ${rssi}` : "RSSI ?";
    return `CONNECTED | ${rssiPart} | ${ageSec}s ago`;
  }

  async _updateDeviceStatusText(nowMs, force = false) {
    const text = this._formatStatusText(nowMs);
    if (!force && text === this._lastStatusText) return;
    this._lastStatusText = text;
    await this.lumia.setVariable("device_status_text", text);
  }

  _startStatusTimer() {
    if (this._statusTimer) return;
    this._statusTimer = setInterval(() => void this._tickStatusTimer(), 5000);
  }

  _stopStatusTimer() {
    if (!this._statusTimer) return;
    clearInterval(this._statusTimer);
    this._statusTimer = null;
  }

  async _tickStatusTimer() {
    try {
      const nowMs = safeNowMs();
      const offlineMs = this._getOfflineTimeoutMs();
      if (this._deviceConnected && this._lastSeenMs && (nowMs - this._lastSeenMs) > offlineMs) {
        await this._setDeviceConnected(false);
      }
      await this._updateDeviceStatusText(nowMs);
    } catch {}
  }

  async _applyPetVariables(payload) {
    if (!payload || typeof payload !== "object") return;

    const changeCode = Number.isFinite(Number(payload.changeCode)) ? Number(payload.changeCode) : 0;
    const fieldId = Number.isFinite(Number(payload.fieldId)) ? Number(payload.fieldId) : 0;
    const fieldName = String(payload.fieldName || PET_FIELD_NAMES[fieldId] || "");
    const changeName = String(payload.changeName || PET_NAMES[changeCode] || "unknown");

    const updates = {
      pet_change_name: changeName,
      pet_change_code: changeCode,
      pet_variation: Number.isFinite(Number(payload.variation)) ? Number(payload.variation) : changeCode,
      pet_field_id: fieldId,
      pet_field_name: fieldName,
      pet_from: Number.isFinite(Number(payload.from)) ? Number(payload.from) : 0,
      pet_to: Number.isFinite(Number(payload.to)) ? Number(payload.to) : 0,
      pet_delta: Number.isFinite(Number(payload.delta)) ? Number(payload.delta) : 0,
      pet_ui_mode: typeof payload.uiMode === "string" ? payload.uiMode : "",
      pet_mode_enabled: Boolean(payload.petModeEnabled),
      pet_alive: Boolean(payload.alive),
      pet_stage: Number.isFinite(Number(payload.stage)) ? Number(payload.stage) : 0,
      pet_stage_name: String(payload.stageName || ""),
      pet_age_minutes: Number.isFinite(Number(payload.ageMinutes)) ? Number(payload.ageMinutes) : 0,
      pet_hunger: Number.isFinite(Number(payload.hunger)) ? Number(payload.hunger) : 0,
      pet_happiness: Number.isFinite(Number(payload.happiness)) ? Number(payload.happiness) : 0,
      pet_energy: Number.isFinite(Number(payload.energy)) ? Number(payload.energy) : 0,
      pet_hygiene: Number.isFinite(Number(payload.hygiene)) ? Number(payload.hygiene) : 0,
      pet_health: Number.isFinite(Number(payload.health)) ? Number(payload.health) : 0,
      pet_discipline: Number.isFinite(Number(payload.discipline)) ? Number(payload.discipline) : 0,
      pet_poop: Number.isFinite(Number(payload.poop)) ? Number(payload.poop) : 0,
      pet_sick: Boolean(payload.sick),
      pet_sleeping: Boolean(payload.sleeping),
      pet_event_base: Number.isFinite(Number(payload.petEventBase)) ? Number(payload.petEventBase) : PET_EVENT_BASE,
      pet_event_count: Number.isFinite(Number(payload.petEventCount)) ? Number(payload.petEventCount) : PET_EVENT_COUNT,
      pet_queue_depth: Number.isFinite(Number(payload.petQueueDepth)) ? Number(payload.petQueueDepth) : 0,
      pet_last_change_code: Number.isFinite(Number(payload.lastPetEventCode)) ? Number(payload.lastPetEventCode) : changeCode,
    };

    for (const [name, value] of Object.entries(updates)) {
      await this.lumia.setVariable(name, value);
    }
  }

  async actions(config) {
    const baseUrl = normalizeBaseUrl(this.settings?.baseUrl);
    const timeoutMs = Number(this.settings?.timeoutMs ?? 2000);
    const uiMode = String(this.settings?.uiMode ?? "legacy_get");
    const msgPath = normalizePath(this.settings?.msgPath ?? "/msg");
    const statusPath = normalizePath(this.settings?.statusPath ?? "/status");
    const clearPath = normalizePath(this.settings?.clearPath ?? "/clear");
    const uiPath = normalizePath(this.settings?.uiPath ?? "/ui");
    const petPath = normalizePath(this.settings?.petPath ?? "/pet");
    const verbosity = this._toastVerbosity();

    const acts = Array.isArray(config?.actions) ? config.actions : [];
    for (const action of acts) {
      const type = action?.type;

      if (type === "clear_screen") {
        if (!baseUrl) throw new Error("ESP Base URL is required for display actions.");
        if (uiMode === "ui_post") await httpPostJson(joinUrl(baseUrl, uiPath), { channel: "clear" }, timeoutMs);
        else await httpGet(joinUrl(baseUrl, clearPath), timeoutMs);
        if (verbosity === "verbose") await this._toast("Display: clear", 1500, true);
        continue;
      }

      if (type === "pet_action") {
        if (!baseUrl) throw new Error("ESP Base URL is required for pet actions.");
        const petAction = String(action?.value?.petAction ?? "status").trim() || "status";
        const raw = await httpGet(`${joinUrl(baseUrl, petPath)}?action=${encodeURIComponent(petAction)}`, timeoutMs);
        const parsed = parseJsonSafe(raw);
        if (parsed) await this._applyPetVariables(parsed);
        if (verbosity === "verbose") await this._toast(`Pet: ${petAction}`, 1500, true);
        continue;
      }

      const msg = String(action?.value?.message ?? action?.value?.text ?? "").trim();
      if (!msg) continue;
      if (!baseUrl) throw new Error("ESP Base URL is required for display actions.");

      if (type === "display_message") {
        if (uiMode === "ui_post") {
          await httpPostJson(joinUrl(baseUrl, uiPath), { channel: "chat", text: msg }, timeoutMs);
        } else {
          await httpGet(`${joinUrl(baseUrl, msgPath)}?t=${encodeURIComponent(msg)}`, timeoutMs);
        }
        if (verbosity === "verbose") await this._toast("Display: message sent", 1500, true);
        continue;
      }

      if (type === "status_message") {
        if (uiMode === "ui_post") {
          await httpPostJson(joinUrl(baseUrl, uiPath), { channel: "status", text: msg }, timeoutMs);
        } else {
          await httpGet(`${joinUrl(baseUrl, statusPath)}?t=${encodeURIComponent(msg)}`, timeoutMs);
        }
        if (verbosity === "verbose") await this._toast("Display: status set", 1500, true);
        continue;
      }
    }
  }

  async _startServer() {
    if (this._server) return;
    const port = this._getPort();
    this._server = http.createServer((req, res) => void this._handleRequest(req, res));
    await new Promise((resolve, reject) => {
      this._server.once("error", reject);
      this._server.listen(port, "0.0.0.0", () => resolve());
    });
  }

  async _stopServer() {
    if (!this._server) return;
    const server = this._server;
    this._server = null;
    await new Promise((resolve) => server.close(() => resolve()));
  }

  async _handleRequest(req, res) {
    try {
      if (req.method === "GET" && req.url === "/health") {
        return this._sendJson(res, 200, {
          ok: true,
          name: this.manifest?.name ?? "plugin",
          version: this.manifest?.version ?? "",
        });
      }

      if (req.method !== "POST" || req.url !== "/event") {
        return this._sendJson(res, 404, { ok: false, error: "Not found" });
      }

      const expectedSecret = this._getSecret();
      if (expectedSecret) {
        const headerValue = String(req.headers["x-matrix-secret"] ?? "").trim();
        if (!headerValue || headerValue !== expectedSecret) {
          return this._sendJson(res, 401, { ok: false, error: "Unauthorized" });
        }
      }

      const body = await this._readJsonBody(req);
      const eventNumber = Number(body?.event);
      const seq = Number.isInteger(Number(body?.seq)) ? Number(body.seq) : null;
      const deviceId = typeof body?.deviceId === "string" ? body.deviceId : "";
      const heldMs = Number.isInteger(Number(body?.heldMs)) ? Number(body.heldMs) : 0;
      const rssi = Number.isFinite(Number(body?.rssi)) ? Number(body.rssi) : 0;
      const ackPayload = { ok: true, ...(seq !== null ? { seq } : {}) };

      if (!Number.isInteger(eventNumber) || eventNumber < MATRIX_EVENT_MIN || eventNumber > EVENT_MAX) {
        return this._sendJson(res, 400, { ok: false, error: "Invalid event number", ...(seq !== null ? { seq } : {}) });
      }

      if (deviceId && seq !== null) {
        const prev = this._lastSeqByDevice.get(deviceId) ?? 0;
        if (seq <= prev) {
          const nowMs = safeNowMs();
          const gapMs = this._lastSeenMs ? (nowMs - this._lastSeenMs) : Number.MAX_SAFE_INTEGER;
          const looksLikeReboot = (seq === 1) || (gapMs > SEQ_RESET_GAP_MS);
          if (!looksLikeReboot) return this._sendJson(res, 200, ackPayload);
          this._lastSeqByDevice.set(deviceId, 0);
        }
      }

      const nowMs = safeNowMs();
      const receivedAt = new Date(nowMs).toISOString();
      const remoteIp = String(req.socket?.remoteAddress ?? "").replace(/^::ffff:/, "");

      this._lastSeenMs = nowMs;
      this._lastRssi = rssi;
      this._deviceId = deviceId;
      this._deviceIp = remoteIp;

      await this._setDeviceConnected(true);
      await this.lumia.setVariable("device_id", deviceId);
      await this.lumia.setVariable("device_ip", remoteIp);
      await this.lumia.setVariable("device_last_seen", receivedAt);
      await this.lumia.setVariable("device_rssi", rssi);
      await this.lumia.setVariable("seq", seq ?? 0);
      await this.lumia.setVariable("held_ms", heldMs);
      await this._updateDeviceStatusText(nowMs, true);

      if (eventNumber <= MATRIX_EVENT_MAX) {
        const isLong = eventNumber >= KEY_COUNT;
        const keyIndex = isLong ? (eventNumber - KEY_COUNT) : eventNumber;
        const kind = isLong ? "long" : "short";
        const alertKey = isLong ? "matrix_6x6_long" : "matrix_6x6_short";

        await this.lumia.setVariable("event", keyIndex);
        await this.lumia.setVariable("kind", kind);
        await this.lumia.setVariable("received_at", receivedAt);

        const keyLabel = this._getKeyLabel(kind, keyIndex);
        await this.lumia.setVariable("key_label", keyLabel);

        if (this._toastVerbosity() === "verbose") {
          const label = keyLabel ? keyLabel : `Key ${keyIndex}`;
          await this._toast(`Input: ${label} (${kind})`, 1200, true);
        }

        await this.lumia.triggerAlert({
          alert: alertKey,
          dynamic: { value: String(keyIndex) },
          extraSettings: {
            event: keyIndex,
            kind,
            received_at: receivedAt,
            device_id: deviceId,
            device_ip: remoteIp,
            device_last_seen: receivedAt,
            device_rssi: rssi,
            device_connected: true,
            seq: seq ?? 0,
            held_ms: heldMs,
            key_label: keyLabel,
            device_status_text: this._lastStatusText,
          },
        });

        if (deviceId && seq !== null) this._lastSeqByDevice.set(deviceId, seq);
        return this._sendJson(res, 200, ackPayload);
      }

      const petCode = Number.isFinite(Number(body?.changeCode)) ? Number(body.changeCode) : (eventNumber - PET_EVENT_BASE);
      const changeName = String(body?.changeName || PET_NAMES[petCode] || "unknown");
      const fieldId = Number.isFinite(Number(body?.fieldId)) ? Number(body.fieldId) : -1;
      const fieldName = String(body?.fieldName || PET_FIELD_NAMES[fieldId] || "");

      await this._applyPetVariables(body);

      if (this._toastVerbosity() === "verbose") {
        await this._toast(`Pet: ${changeName}`, 1200, true);
      }

      await this.lumia.triggerAlert({
        alert: "pet",
        dynamic: { value: changeName },
        extraSettings: {
          pet_change_name: changeName,
          pet_change_code: petCode,
          pet_variation: Number.isFinite(Number(body?.variation)) ? Number(body.variation) : petCode,
          pet_field_id: fieldId,
          pet_field_name: fieldName,
          pet_from: Number.isFinite(Number(body?.from)) ? Number(body.from) : 0,
          pet_to: Number.isFinite(Number(body?.to)) ? Number(body.to) : 0,
          pet_delta: Number.isFinite(Number(body?.delta)) ? Number(body.delta) : 0,
          pet_ui_mode: typeof body?.uiMode === "string" ? body.uiMode : "",
          pet_mode_enabled: Boolean(body?.petModeEnabled),
          pet_alive: Boolean(body?.alive),
          pet_stage: Number.isFinite(Number(body?.stage)) ? Number(body.stage) : 0,
          pet_stage_name: String(body?.stageName || ""),
          pet_age_minutes: Number.isFinite(Number(body?.ageMinutes)) ? Number(body.ageMinutes) : 0,
          pet_hunger: Number.isFinite(Number(body?.hunger)) ? Number(body.hunger) : 0,
          pet_happiness: Number.isFinite(Number(body?.happiness)) ? Number(body.happiness) : 0,
          pet_energy: Number.isFinite(Number(body?.energy)) ? Number(body.energy) : 0,
          pet_hygiene: Number.isFinite(Number(body?.hygiene)) ? Number(body.hygiene) : 0,
          pet_health: Number.isFinite(Number(body?.health)) ? Number(body.health) : 0,
          pet_discipline: Number.isFinite(Number(body?.discipline)) ? Number(body.discipline) : 0,
          pet_poop: Number.isFinite(Number(body?.poop)) ? Number(body.poop) : 0,
          pet_sick: Boolean(body?.sick),
          pet_sleeping: Boolean(body?.sleeping),
          pet_event_base: Number.isFinite(Number(body?.petEventBase)) ? Number(body.petEventBase) : PET_EVENT_BASE,
          pet_event_count: Number.isFinite(Number(body?.petEventCount)) ? Number(body.petEventCount) : PET_EVENT_COUNT,
          pet_queue_depth: Number.isFinite(Number(body?.petQueueDepth)) ? Number(body.petQueueDepth) : 0,
          pet_last_change_code: Number.isFinite(Number(body?.lastPetEventCode)) ? Number(body.lastPetEventCode) : petCode,
          device_id: deviceId,
          device_ip: remoteIp,
          device_last_seen: receivedAt,
          device_rssi: rssi,
          device_connected: true,
          seq: seq ?? 0,
          device_status_text: this._lastStatusText,
        },
      });

      if (deviceId && seq !== null) this._lastSeqByDevice.set(deviceId, seq);
      return this._sendJson(res, 200, ackPayload);
    } catch (error) {
      const message = error instanceof Error ? error.message : String(error);
      try {
        await this.lumia.log({ message: `[Lumi-Con v1.1.0] ${message}`, level: "error" });
      } catch {}
      return this._sendJson(res, 500, { ok: false, error: "Server error" });
    }
  }

  _readJsonBody(req) {
    return new Promise((resolve, reject) => {
      let data = "";
      req.on("data", (chunk) => {
        data += chunk;
        if (data.length > 1024 * 32) reject(new Error("Body too large"));
      });
      req.on("end", () => {
        if (!data) return resolve({});
        try {
          resolve(JSON.parse(data));
        } catch {
          resolve({});
        }
      });
      req.on("error", reject);
    });
  }

  _sendJson(res, status, obj) {
    try {
      const payload = JSON.stringify(obj);
      res.writeHead(status, {
        "Content-Type": "application/json",
        "Content-Length": Buffer.byteLength(payload),
      });
      res.end(payload);
    } catch {
      res.writeHead(500);
      res.end();
    }
  }
};