const { Plugin } = require("@lumiastream/plugin");
const crypto = require("node:crypto");
const dgram = require("node:dgram");
const http = require("node:http");
const DEVICE = require("./device-config.json");

const KEY_COUNT = Number(DEVICE.keyCount);
const MATRIX_EVENT_MIN = 0;
const MATRIX_EVENT_MAX = (KEY_COUNT * 2) - 1;
const PET_EVENT_BASE = KEY_COUNT * 2;
const PET_EVENT_COUNT = 22;
const EVENT_MAX = PET_EVENT_BASE + PET_EVENT_COUNT - 1;
const DEFAULT_PORT = Number(DEVICE.listenPort);
const SEQ_RESET_GAP_MS = 5000;
const STATUS_TICK_MS = 5000;
const RETRY_MIN_MS = 5000;
const RETRY_MAX_MS = 60000;
const MAX_DEVICE_STATES = 64;
const MAX_BODY_BYTES = 32 * 1024;

const PET_NAMES = [
  "hunger_up",
  "hunger_down",
  "happiness_up",
  "happiness_down",
  "energy_up",
  "energy_down",
  "hygiene_up",
  "hygiene_down",
  "health_up",
  "health_down",
  "discipline_up",
  "discipline_down",
  "poop_up",
  "poop_down",
  "stage_up",
  "stage_down",
  "sleep_on",
  "sleep_off",
  "sick_on",
  "sick_off",
  "alive_on",
  "alive_off",
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

function hasOwn(value, key) {
  return Boolean(value) && Object.prototype.hasOwnProperty.call(value, key);
}

function parseInteger(value, fallback = null) {
  if (typeof value === "number") {
    return Number.isSafeInteger(value) ? value : fallback;
  }
  if (typeof value === "string" && /^-?\d+$/.test(value.trim())) {
    const parsed = Number(value);
    return Number.isSafeInteger(parsed) ? parsed : fallback;
  }
  return fallback;
}

function parseFiniteNumber(value, fallback = null) {
  if (typeof value === "number") {
    return Number.isFinite(value) ? value : fallback;
  }
  if (
    typeof value === "string" &&
    value.trim() !== "" &&
    Number.isFinite(Number(value))
  ) {
    return Number(value);
  }
  return fallback;
}

function parseBoolean(value, fallback = false) {
  if (typeof value === "boolean") return value;
  if (value === 1 || value === "1" || value === "true") return true;
  if (value === 0 || value === "0" || value === "false") return false;
  return fallback;
}

function safeNowMs() {
  return Date.now();
}

function msToAgeSeconds(ms) {
  return Math.max(0, Math.floor(ms / 1000));
}

function normalizeBaseUrl(baseUrl) {
  const raw = String(baseUrl || "").trim().replace(/\/+$/, "");
  if (!raw || !/^https?:\/\//i.test(raw)) return "";
  try {
    const parsed = new URL(raw);
    if (parsed.protocol !== "http:" && parsed.protocol !== "https:") return "";
    return parsed.toString().replace(/\/+$/, "");
  } catch {
    return "";
  }
}

function normalizePath(value) {
  const raw = String(value || "").trim();
  if (!raw) return "/";
  return raw.startsWith("/") ? raw : `/${raw}`;
}

function joinUrl(base, path) {
  const normalizedBase = String(base || "").replace(/\/+$/, "");
  const normalizedPath = String(path || "");
  return `${normalizedBase}${normalizedPath.startsWith("/") ? normalizedPath : `/${normalizedPath}`}`;
}

function normalizeStatusColor(value) {
  const color = String(value || "").trim().toLowerCase();
  return color === "red" || color === "yellow" || color === "green"
    ? color
    : "";
}

function normalizeCelebrationStyle(value) {
  const style = String(value || "").trim().toLowerCase();
  return style === "pulse" || style === "success" ? style : "confetti";
}

function normalizeCelebrationColor(value) {
  const color = String(value || "").trim().toLowerCase();
  return ["teal", "green", "yellow", "gold", "red", "pink", "magenta"].includes(color)
    ? color
    : "teal";
}

function normalizeCelebrationDuration(value) {
  const duration = Number(value);
  if (!Number.isFinite(duration)) return 1800;
  return Math.max(600, Math.min(5000, Math.round(duration)));
}

function normalizeScreenMode(value) {
  const mode = String(value || "").trim().toLowerCase();
  return mode === "pet" || mode === "debug" ? mode : "chat";
}

function parseKeyLabels(text) {
  const raw = typeof text === "string" ? text : "";
  const lines = raw.split(/\r?\n/).map((line) => line.trim());
  const labels = new Array(KEY_COUNT).fill("");
  for (let index = 0; index < Math.min(KEY_COUNT, lines.length); index += 1) {
    labels[index] = lines[index];
  }
  return labels;
}

function parseJsonSafe(text) {
  try {
    return JSON.parse(text);
  } catch {
    return null;
  }
}

function errorMessage(error) {
  return error instanceof Error ? error.message : String(error);
}

function httpError(message, statusCode) {
  const error = new Error(message);
  error.statusCode = statusCode;
  return error;
}

function secretsMatch(expected, received) {
  const expectedBuffer = Buffer.from(String(expected), "utf8");
  const receivedBuffer = Buffer.from(String(received), "utf8");
  if (expectedBuffer.length !== receivedBuffer.length) return false;
  return crypto.timingSafeEqual(expectedBuffer, receivedBuffer);
}

async function fetchText(url, options, timeoutMs) {
  const requestedMs = Number(timeoutMs);
  const safeMs =
    Number.isFinite(requestedMs) && requestedMs >= 250
      ? Math.floor(requestedMs)
      : 2000;
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), safeMs);

  try {
    const response = await fetch(url, {
      ...options,
      signal: controller.signal,
    });
    const body = await response.text().catch(() => "");
    if (!response.ok) {
      throw new Error(`HTTP ${response.status}${body ? `: ${body}` : ""}`);
    }
    return body;
  } catch (error) {
    if (error?.name === "AbortError") {
      throw new Error(`Request timed out after ${safeMs}ms`);
    }
    throw error;
  } finally {
    clearTimeout(timer);
  }
}

function httpGet(url, timeoutMs) {
  return fetchText(url, { method: "GET" }, timeoutMs);
}

function httpPostJson(url, value, timeoutMs) {
  return fetchText(
    url,
    {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(value),
    },
    timeoutMs,
  );
}

function hasPetChangeData(payload) {
  return [
    "changeCode",
    "changeName",
    "fieldId",
    "fieldName",
    "from",
    "to",
    "delta",
    "variation",
  ].some((key) => hasOwn(payload, key));
}

module.exports = class LumiConPlugin extends Plugin {
  constructor(manifest, context) {
    super(manifest, context);

    this._settingsCache = {};
    this._server = null;
    this._serverStartPromise = null;
    this._statusTimer = null;
    this._statusTickInFlight = false;
    this._offlineProbeInFlight = false;
    this._registerPromise = null;

    this._deviceStates = new Map();
    this._inFlightEvents = new Set();

    this._deviceConnected = false;
    this._deviceId = "";
    this._deviceIp = "";
    this._lastRssi = 0;
    this._lastSeenMs = 0;
    this._lastStatusText = "";

    this._listenerStatus = "stopped";
    this._listenerError = "";
    this._nextListenerRetryMs = 0;
    this._listenerRetryDelayMs = RETRY_MIN_MS;
    this._nextReconnectMs = 0;
    this._reconnectDelayMs = RETRY_MIN_MS;

    this._labelsShort = new Array(KEY_COUNT).fill("");
    this._labelsLong = new Array(KEY_COUNT).fill("");
    this._labelsFallback = new Array(KEY_COUNT).fill("");

    this._lastToastMs = 0;
    this._lastVerboseToastMs = 0;
  }

  async onload() {
    this._settingsCache = { ...(this.settings || {}) };
    this._refreshKeyLabels(this._settingsCache);

    await this._safeSetVariables({
      device_connected: false,
      device_status_text: "OFFLINE",
      host_pc_ip: "",
      host_register_status: "idle",
      host_register_message: "",
      host_register_at: "",
      listener_status: "stopped",
      listener_error: "",
      listener_port: this._getPort(),
      firmware_plugin_port: 0,
      firmware_version: "",
      device_free_heap: 0,
    });

    this._startStatusTimer();

    if (!this._isEnabled()) {
      await this._updateDeviceStatusText(safeNowMs(), true);
      return;
    }

    const listening = await this._ensureServer();
    if (listening && this._getBaseUrl()) {
      await this._registerDeviceHost({
        showSuccessToast: false,
        showFailureToast: false,
      });
    }
  }

  async onunload() {
    this._stopStatusTimer();
    await this._stopServer({ updateState: true });
    await this._setDeviceConnected(false, { silent: true });
  }

  async onsettingsupdate(settings, previousSettings) {
    const previous = { ...(previousSettings || this._settingsCache || {}) };
    this._settingsCache = { ...(settings || {}) };
    this._refreshKeyLabels(this._settingsCache);

    const enabledChanged =
      Boolean(this._settingsCache.enabled ?? true) !==
      Boolean(previous.enabled ?? true);
    const portChanged =
      this._getPort(this._settingsCache) !== this._getPort(previous);
    const baseUrlChanged =
      normalizeBaseUrl(this._settingsCache.baseUrl) !==
      normalizeBaseUrl(previous.baseUrl);
    const registerPathChanged =
      normalizePath(this._settingsCache.registerPath ?? "/plugin") !==
      normalizePath(previous.registerPath ?? "/plugin");

    if (!this._isEnabled()) {
      await this._stopServer({ updateState: true });
      await this._setDeviceConnected(false, { silent: true });
      await this._updateDeviceStatusText(safeNowMs(), true);
      return;
    }

    if (enabledChanged || portChanged) {
      await this._stopServer({ updateState: false });
    }

    const listening = await this._ensureServer();
    if (
      listening &&
      this._getBaseUrl() &&
      (enabledChanged || portChanged || baseUrlChanged || registerPathChanged)
    ) {
      await this._registerDeviceHost({
        showSuccessToast: true,
        showFailureToast: true,
      });
    }

    await this._updateDeviceStatusText(safeNowMs(), true);
  }

  _getSettings() {
    return this._settingsCache || {};
  }

  _isEnabled() {
    return Boolean(this._getSettings().enabled ?? true);
  }

  _getPort(settings = this._getSettings()) {
    const port = Number(settings?.listenPort ?? DEFAULT_PORT);
    return Number.isInteger(port) && port > 0 && port <= 65535
      ? port
      : DEFAULT_PORT;
  }

  _getSecret() {
    return String(this._getSettings().secret ?? "").trim();
  }

  _getOfflineTimeoutMs() {
    const seconds = Number(this._getSettings().offlineTimeoutSec ?? 30);
    const safeSeconds =
      Number.isFinite(seconds) && seconds >= 5 ? seconds : 30;
    return Math.floor(safeSeconds * 1000);
  }

  _getTimeoutMs() {
    const milliseconds = Number(this._getSettings().timeoutMs ?? 2000);
    return Number.isFinite(milliseconds) && milliseconds >= 250
      ? Math.floor(milliseconds)
      : 2000;
  }

  _getBaseUrl() {
    return normalizeBaseUrl(this._getSettings().baseUrl);
  }

  _getStatusPath() {
    return normalizePath(this._getSettings().statusPath ?? "/status");
  }

  _getRegisterPath() {
    return normalizePath(this._getSettings().registerPath ?? "/plugin");
  }

  _debugToastsEnabled() {
    return Boolean(this._getSettings().debugToasts ?? false);
  }

  _toastVerbosity() {
    return String(this._getSettings().toastVerbosity ?? "important") ===
      "verbose"
      ? "verbose"
      : "important";
  }

  _refreshKeyLabels(settings = this._getSettings()) {
    this._labelsShort = parseKeyLabels(settings?.keyLabelsShort);
    this._labelsLong = parseKeyLabels(settings?.keyLabelsLong);
    this._labelsFallback = parseKeyLabels(settings?.keyLabels);
  }

  _getKeyLabel(kind, keyIndex) {
    const index = Number(keyIndex);
    if (!Number.isInteger(index) || index < 0 || index >= KEY_COUNT) return "";
    const labels = kind === "long" ? this._labelsLong : this._labelsShort;
    return String(labels[index] || this._labelsFallback[index] || "").trim();
  }

  async _log(message, level = "info") {
    try {
      await this.lumia.log({
        message: `[${DEVICE.logName} v${this.manifest?.version || ""}] ${message}`,
        level,
      });
    } catch {}
  }

  async _setVariables(updates) {
    const entries = Object.entries(updates || {});
    if (!entries.length) return;
    await Promise.all(
      entries.map(([name, value]) => this.lumia.setVariable(name, value)),
    );
  }

  async _safeSetVariables(updates) {
    try {
      await this._setVariables(updates);
    } catch (error) {
      await this._log(`Variable update failed: ${errorMessage(error)}`, "error");
    }
  }

  async _toast(message, timeMs = 2500, isVerbose = false, force = false) {
    if (!force && !this._debugToastsEnabled()) return;

    const nowMs = safeNowMs();
    if (!force && nowMs - this._lastToastMs < 800) return;
    if (!force && isVerbose && nowMs - this._lastVerboseToastMs < 250) return;

    this._lastToastMs = nowMs;
    if (isVerbose) this._lastVerboseToastMs = nowMs;

    try {
      await this.lumia.showToast({ message: String(message), time: timeMs });
    } catch {}
  }

  async _setListenerState(status, error = "") {
    this._listenerStatus = status;
    this._listenerError = error;
    await this._safeSetVariables({
      listener_status: status,
      listener_error: error,
      listener_port: this._getPort(),
    });
  }

  async _setDeviceConnected(isConnected, options = {}) {
    const connected = Boolean(isConnected);
    const silent = Boolean(options.silent);
    const failureToast = Boolean(options.failureToast);

    if (connected) this._resetReconnectSchedule();
    if (this._deviceConnected === connected) return;

    this._deviceConnected = connected;
    await this._safeSetVariables({ device_connected: connected });

    try {
      await this.lumia.updateConnection(connected);
    } catch {}

    if (!silent && !connected && failureToast) {
      await this._toast(`${DEVICE.displayName} offline`, 2500, false);
    }
  }

  _formatStatusText(nowMs) {
    if (!this._isEnabled()) return "LISTENER DISABLED";
    if (this._listenerStatus === "error") {
      return `LISTENER ERROR | PORT ${this._getPort()}`;
    }
    if (!this._server?.listening) {
      return `LISTENER STARTING | PORT ${this._getPort()}`;
    }
    if (!this._deviceConnected || !this._lastSeenMs) {
      return `OFFLINE | LISTENING ${this._getPort()}`;
    }

    const ageSeconds = msToAgeSeconds(nowMs - this._lastSeenMs);
    const rssi = Number(this._lastRssi);
    const rssiText =
      Number.isFinite(rssi) && rssi !== 0 ? `RSSI ${rssi}` : "RSSI ?";
    return `CONNECTED | ${rssiText} | ${ageSeconds}s ago`;
  }

  async _updateDeviceStatusText(nowMs, force = false) {
    const text = this._formatStatusText(nowMs);
    if (!force && text === this._lastStatusText) return;
    this._lastStatusText = text;
    await this._safeSetVariables({ device_status_text: text });
  }

  _startStatusTimer() {
    if (this._statusTimer) return;
    this._statusTimer = setInterval(
      () => void this._tickStatusTimer(),
      STATUS_TICK_MS,
    );
  }

  _stopStatusTimer() {
    if (!this._statusTimer) return;
    clearInterval(this._statusTimer);
    this._statusTimer = null;
  }

  _scheduleListenerRetry() {
    this._nextListenerRetryMs = safeNowMs() + this._listenerRetryDelayMs;
    this._listenerRetryDelayMs = Math.min(
      this._listenerRetryDelayMs * 2,
      RETRY_MAX_MS,
    );
  }

  _resetListenerRetrySchedule() {
    this._nextListenerRetryMs = 0;
    this._listenerRetryDelayMs = RETRY_MIN_MS;
  }

  _scheduleReconnect() {
    this._nextReconnectMs = safeNowMs() + this._reconnectDelayMs;
    this._reconnectDelayMs = Math.min(
      this._reconnectDelayMs * 2,
      RETRY_MAX_MS,
    );
  }

  _resetReconnectSchedule() {
    this._nextReconnectMs = 0;
    this._reconnectDelayMs = RETRY_MIN_MS;
  }

  async _tickStatusTimer() {
    if (this._statusTickInFlight) return;
    this._statusTickInFlight = true;

    try {
      const nowMs = safeNowMs();

      if (!this._isEnabled()) {
        await this._updateDeviceStatusText(nowMs);
        return;
      }

      if (!this._server?.listening) {
        if (nowMs >= this._nextListenerRetryMs) {
          const listening = await this._ensureServer();
          if (listening && this._getBaseUrl()) {
            await this._registerDeviceHost({
              showSuccessToast: false,
              showFailureToast: false,
            });
          }
        }
        await this._updateDeviceStatusText(nowMs);
        return;
      }

      if (
        this._deviceConnected &&
        this._lastSeenMs &&
        nowMs - this._lastSeenMs > this._getOfflineTimeoutMs()
      ) {
        await this._silentHealthCheck();
      } else if (
        !this._deviceConnected &&
        this._getBaseUrl() &&
        nowMs >= this._nextReconnectMs
      ) {
        await this._registerDeviceHost({
          showSuccessToast: false,
          showFailureToast: false,
        });
      }

      await this._updateDeviceStatusText(nowMs);
    } catch (error) {
      await this._log(`Status timer failed: ${errorMessage(error)}`, "error");
    } finally {
      this._statusTickInFlight = false;
    }
  }

  async _ensureServer() {
    try {
      await this._startServer();
      return Boolean(this._server?.listening);
    } catch {
      return false;
    }
  }

  async _startServer() {
    if (this._server?.listening) return;
    if (this._serverStartPromise) return this._serverStartPromise;

    this._serverStartPromise = this._startServerInternal();
    try {
      await this._serverStartPromise;
    } finally {
      this._serverStartPromise = null;
    }
  }

  async _startServerInternal() {
    const port = this._getPort();
    await this._setListenerState("starting", "");

    const server = http.createServer((request, response) => {
      void this._handleRequest(request, response);
    });
    server.requestTimeout = 5000;
    server.headersTimeout = 6000;
    server.keepAliveTimeout = 1000;
    this._server = server;

    try {
      await new Promise((resolve, reject) => {
        const onError = (error) => {
          server.off("listening", onListening);
          reject(error);
        };
        const onListening = () => {
          server.off("error", onError);
          resolve();
        };

        server.once("error", onError);
        server.once("listening", onListening);
        server.listen(port, "0.0.0.0");
      });
    } catch (error) {
      if (this._server === server) this._server = null;
      try {
        server.close();
      } catch {}
      const message = errorMessage(error);
      await this._setListenerState("error", message);
      this._scheduleListenerRetry();
      await this._log(`Listener failed on port ${port}: ${message}`, "error");
      throw error;
    }

    server.on("error", (error) => {
      void this._handleRuntimeServerError(server, error);
    });

    this._resetListenerRetrySchedule();
    await this._setListenerState("listening", "");
  }

  async _handleRuntimeServerError(server, error) {
    if (this._server !== server) return;
    this._server = null;
    try {
      server.closeAllConnections?.();
      server.close();
    } catch {}
    const message = errorMessage(error);
    await this._setListenerState("error", message);
    this._scheduleListenerRetry();
    await this._setDeviceConnected(false, { silent: true });
    await this._log(`Listener runtime error: ${message}`, "error");
  }

  async _stopServer(options = {}) {
    if (this._serverStartPromise) {
      try {
        await this._serverStartPromise;
      } catch {}
    }

    const server = this._server;
    this._server = null;

    if (server) {
      await new Promise((resolve) => {
        let settled = false;
        const finish = () => {
          if (settled) return;
          settled = true;
          clearTimeout(timer);
          resolve();
        };
        const timer = setTimeout(() => {
          try {
            server.closeAllConnections?.();
          } catch {}
          finish();
        }, 1500);

        try {
          server.close(() => finish());
          server.closeIdleConnections?.();
        } catch {
          finish();
        }
      });
    }

    if (options.updateState !== false) {
      await this._setListenerState("stopped", "");
    }
  }

  async _resolveHostPcIp(baseUrl) {
    const url = new URL(baseUrl);
    const targetPort =
      url.port && Number(url.port) > 0
        ? Number(url.port)
        : url.protocol === "https:"
          ? 443
          : 80;

    return new Promise((resolve, reject) => {
      const socket = dgram.createSocket("udp4");
      let settled = false;

      const finish = (error, value) => {
        if (settled) return;
        settled = true;
        clearTimeout(timer);
        try {
          socket.close();
        } catch {}
        if (error) reject(error);
        else resolve(value);
      };

      const timer = setTimeout(
        () => finish(new Error("Timed out resolving this PC's device-facing IP.")),
        this._getTimeoutMs(),
      );

      socket.once("error", (error) => finish(error));
      socket.connect(targetPort, url.hostname, () => {
        try {
          const address = socket.address();
          if (!address || typeof address !== "object" || !address.address) {
            finish(new Error("Could not determine this PC IP for the device."));
            return;
          }
          finish(null, address.address);
        } catch (error) {
          finish(error);
        }
      });
    });
  }

  async _registerDeviceHost(options = {}) {
    if (this._registerPromise) return this._registerPromise;
    this._registerPromise = this._performDeviceRegistration(options);
    try {
      return await this._registerPromise;
    } finally {
      this._registerPromise = null;
    }
  }

  async _performDeviceRegistration(options = {}) {
    const baseUrl = this._getBaseUrl();
    const showSuccessToast = Boolean(options.showSuccessToast);
    const showFailureToast = Boolean(options.showFailureToast);

    if (!baseUrl) {
      await this._safeSetVariables({
        host_pc_ip: "",
        host_register_status: "idle",
        host_register_message: "ESP Base URL not set.",
        host_register_at: "",
      });
      return false;
    }

    if (!this._isEnabled() || !this._server?.listening) {
      const message = "The listener must be enabled and running before connecting.";
      await this._safeSetVariables({
        host_register_status: "failed",
        host_register_message: message,
        host_register_at: new Date().toISOString(),
      });
      if (showFailureToast) {
        await this._toast(message, 5000, false, true);
      }
      return false;
    }

    try {
      const pcIp = await this._resolveHostPcIp(baseUrl);
      const registerUrl = `${joinUrl(
        baseUrl,
        this._getRegisterPath(),
      )}?host=${encodeURIComponent(pcIp)}`;
      const raw = await httpGet(registerUrl, this._getTimeoutMs());
      const parsed = parseJsonSafe(raw);

      if (parsed?.ok === false) {
        throw new Error(String(parsed.error || "Device rejected host registration."));
      }

      const firmwarePort = parseInteger(parsed?.pluginPort, null);
      if (firmwarePort !== null) {
        await this._safeSetVariables({ firmware_plugin_port: firmwarePort });
        if (firmwarePort !== this._getPort()) {
          throw new Error(
            `Port mismatch: firmware posts to ${firmwarePort}, but this plugin listens on ${this._getPort()}.`,
          );
        }
      }

      const nowIso = new Date().toISOString();
      await this._safeSetVariables({
        host_pc_ip: pcIp,
        host_register_status: "registered",
        host_register_message: "Device host registration confirmed.",
        host_register_at: nowIso,
      });

      const healthy = await this._silentHealthCheck();
      if (!healthy) {
        throw new Error("Host registered, but the device health check failed.");
      }

      this._resetReconnectSchedule();
      if (showSuccessToast) {
        await this._toast(`Device connected to ${pcIp}`, 3500, false, true);
      }
      return true;
    } catch (error) {
      const message = errorMessage(error);
      await this._safeSetVariables({
        host_register_status: "failed",
        host_register_message: message,
        host_register_at: new Date().toISOString(),
      });
      await this._setDeviceConnected(false, { silent: true });
      if (this._nextReconnectMs <= safeNowMs()) {
        this._scheduleReconnect();
      }
      await this._log(`Device connect failed: ${message}`, "error");

      if (showFailureToast) {
        await this._toast(`Device connect failed: ${message}`, 5000, false, true);
      }
      return false;
    }
  }

  async _silentHealthCheck() {
    if (this._offlineProbeInFlight) return this._deviceConnected;

    const baseUrl = this._getBaseUrl();
    if (!baseUrl) {
      await this._setDeviceConnected(false, {
        silent: false,
        failureToast: true,
      });
      this._scheduleReconnect();
      await this._updateDeviceStatusText(safeNowMs(), true);
      return false;
    }

    this._offlineProbeInFlight = true;
    try {
      const url = `${joinUrl(
        baseUrl,
        this._getStatusPath(),
      )}?silent=1&ts=${Date.now()}`;
      const raw = await httpGet(url, this._getTimeoutMs());
      const parsed = parseJsonSafe(raw);
      if (!parsed || parsed.ok !== true) {
        throw new Error("Invalid health response");
      }

      const nowMs = safeNowMs();
      const nowIso = new Date(nowMs).toISOString();
      const deviceId =
        typeof parsed.device_id === "string"
          ? parsed.device_id.trim()
          : this._deviceId;
      const deviceIp =
        typeof parsed.device_ip === "string"
          ? parsed.device_ip.trim()
          : this._deviceIp;
      const rssi = parseFiniteNumber(parsed.device_rssi, this._lastRssi);
      const sequence = parseInteger(parsed.seq, 0);

      this._lastSeenMs = nowMs;
      this._lastRssi = rssi;
      this._deviceId = deviceId;
      this._deviceIp = deviceIp;
      this._reconcileSequenceFromHealth(deviceId, sequence);

      const updates = {
        device_id: deviceId || "",
        device_ip: deviceIp || "",
        device_rssi: rssi || 0,
        seq: sequence,
        device_last_seen: nowIso,
      };
      if (hasOwn(parsed, "fw_version")) {
        updates.firmware_version = String(parsed.fw_version || "");
      }
      if (hasOwn(parsed, "free_heap")) {
        updates.device_free_heap =
          parseInteger(parsed.free_heap, 0) ?? 0;
      }

      await this._setVariables(updates);
      await this._setDeviceConnected(true, { silent: true });
      await this._updateDeviceStatusText(nowMs, true);
      return true;
    } catch {
      await this._setDeviceConnected(false, {
        silent: false,
        failureToast: true,
      });
      this._lastSeenMs = 0;
      this._scheduleReconnect();
      await this._updateDeviceStatusText(safeNowMs(), true);
      return false;
    } finally {
      this._offlineProbeInFlight = false;
    }
  }

  _reconcileSequenceFromHealth(deviceId, sequence) {
    if (!deviceId || !Number.isInteger(sequence)) return;
    const state = this._deviceStates.get(deviceId);
    if (!state || sequence >= state.lastSeq) return;
    this._rememberDeviceState(deviceId, {
      lastSeq: sequence,
      lastUptimeMs: null,
      lastEventSeenMs: 0,
    });
  }

  _rememberDeviceState(deviceId, state) {
    if (!deviceId) return;
    this._deviceStates.delete(deviceId);
    this._deviceStates.set(deviceId, state);
    while (this._deviceStates.size > MAX_DEVICE_STATES) {
      const oldestKey = this._deviceStates.keys().next().value;
      this._deviceStates.delete(oldestKey);
    }
  }

  async _markOutgoingSuccess() {
    const nowMs = safeNowMs();
    const nowIso = new Date(nowMs).toISOString();
    this._lastSeenMs = nowMs;
    await this._safeSetVariables({ device_last_seen: nowIso });
    await this._setDeviceConnected(true, { silent: true });
    await this._updateDeviceStatusText(nowMs, true);
  }

  _buildPetStateUpdates(payload) {
    const updates = {};
    const numberFields = {
      stage: "pet_stage",
      ageMinutes: "pet_age_minutes",
      hunger: "pet_hunger",
      happiness: "pet_happiness",
      energy: "pet_energy",
      hygiene: "pet_hygiene",
      health: "pet_health",
      discipline: "pet_discipline",
      poop: "pet_poop",
      petEventBase: "pet_event_base",
      petEventCount: "pet_event_count",
      petQueueDepth: "pet_queue_depth",
      lastPetEventCode: "pet_last_change_code",
    };
    const booleanFields = {
      petModeEnabled: "pet_mode_enabled",
      alive: "pet_alive",
      sick: "pet_sick",
      sleeping: "pet_sleeping",
    };

    if (hasOwn(payload, "uiMode")) {
      updates.pet_ui_mode = String(payload.uiMode || "");
    }
    if (hasOwn(payload, "stageName")) {
      updates.pet_stage_name = String(payload.stageName || "");
    }

    for (const [source, target] of Object.entries(numberFields)) {
      if (hasOwn(payload, source)) {
        updates[target] = parseFiniteNumber(payload[source], 0);
      }
    }
    for (const [source, target] of Object.entries(booleanFields)) {
      if (hasOwn(payload, source)) {
        updates[target] = parseBoolean(payload[source], false);
      }
    }

    return updates;
  }

  _buildPetChangeUpdates(payload) {
    const changeCodeFromPayload = parseInteger(payload.changeCode, null);
    const namedCode =
      typeof payload.changeName === "string"
        ? PET_NAMES.indexOf(payload.changeName)
        : -1;
    const changeCode =
      changeCodeFromPayload !== null
        ? changeCodeFromPayload
        : namedCode >= 0
          ? namedCode
          : 0;
    const fieldId = parseInteger(payload.fieldId, -1);
    const changeName = String(
      payload.changeName || PET_NAMES[changeCode] || "unknown",
    );
    const fieldName = String(
      payload.fieldName || PET_FIELD_NAMES[fieldId] || "",
    );

    return {
      pet_change_name: changeName,
      pet_change_code: changeCode,
      pet_variation: parseFiniteNumber(payload.variation, changeCode),
      pet_field_id: fieldId,
      pet_field_name: fieldName,
      pet_from: parseFiniteNumber(payload.from, 0),
      pet_to: parseFiniteNumber(payload.to, 0),
      pet_delta: parseFiniteNumber(payload.delta, 0),
    };
  }

  async _applyPetVariables(payload, includeChange = hasPetChangeData(payload)) {
    if (!payload || typeof payload !== "object") return;
    const updates = this._buildPetStateUpdates(payload);
    if (includeChange) {
      Object.assign(updates, this._buildPetChangeUpdates(payload));
    }
    await this._setVariables(updates);
  }

  async actions(config) {
    const settings = this._getSettings();
    const baseUrl = this._getBaseUrl();
    const timeoutMs = this._getTimeoutMs();
    const uiMode =
      String(settings.uiMode ?? "ui_post") === "ui_post"
        ? "ui_post"
        : "legacy_get";
    const msgPath = normalizePath(settings.msgPath ?? "/msg");
    const statusPath = normalizePath(settings.statusPath ?? "/status");
    const clearPath = normalizePath(settings.clearPath ?? "/clear");
    const uiPath = normalizePath(settings.uiPath ?? "/ui");
    const petPath = normalizePath(settings.petPath ?? "/pet");
    const modePath = normalizePath(settings.modePath ?? "/mode");
    const verbosity = this._toastVerbosity();

    const actions = Array.isArray(config?.actions) ? config.actions : [];
    for (const action of actions) {
      const type = action?.type;

      if (type === "connect_device") {
        if (!this._isEnabled()) {
          throw new Error("Enable Listener before connecting the device.");
        }
        if (!(await this._ensureServer())) {
          throw new Error(
            this._listenerError || `Could not open listener port ${this._getPort()}.`,
          );
        }
        const connected = await this._registerDeviceHost({
          showSuccessToast: true,
          showFailureToast: true,
        });
        if (!connected) throw new Error("Device connect failed.");
        continue;
      }

      if (type === "device_screen_mode") {
        if (!baseUrl) {
          throw new Error("ESP Base URL is required for device actions.");
        }
        const screenMode = normalizeScreenMode(action?.value?.screenMode);
        await httpGet(
          `${joinUrl(baseUrl, modePath)}?set=${encodeURIComponent(screenMode)}`,
          timeoutMs,
        );
        await this._markOutgoingSuccess();
        if (verbosity === "verbose") {
          await this._toast(`Device screen: ${screenMode}`, 1500, true);
        }
        continue;
      }

      if (type === "clear_screen") {
        if (!baseUrl) {
          throw new Error("ESP Base URL is required for display actions.");
        }
        if (uiMode === "ui_post") {
          await httpPostJson(
            joinUrl(baseUrl, uiPath),
            { channel: "clear" },
            timeoutMs,
          );
        } else {
          await httpGet(joinUrl(baseUrl, clearPath), timeoutMs);
        }
        await this._markOutgoingSuccess();
        if (verbosity === "verbose") {
          await this._toast("Display: clear", 1500, true);
        }
        continue;
      }

      if (type === "pet_action") {
        if (!baseUrl) {
          throw new Error("ESP Base URL is required for pet actions.");
        }
        const petAction =
          String(action?.value?.petAction ?? "status").trim() || "status";
        const raw = await httpGet(
          `${joinUrl(baseUrl, petPath)}?action=${encodeURIComponent(petAction)}`,
          timeoutMs,
        );
        const parsed = parseJsonSafe(raw);
        if (parsed) {
          await this._applyPetVariables(parsed, hasPetChangeData(parsed));
        }
        await this._markOutgoingSuccess();
        if (verbosity === "verbose") {
          await this._toast(`Pet: ${petAction}`, 1500, true);
        }
        continue;
      }

      if (type === "display_celebration") {
        if (!baseUrl) {
          throw new Error("ESP Base URL is required for display actions.");
        }
        const celebrationText = String(
          action?.value?.message ?? action?.value?.text ?? "NICE!",
        ).trim() || "NICE!";
        const celebrationStyle = normalizeCelebrationStyle(
          action?.value?.celebrationStyle,
        );
        const celebrationColor = normalizeCelebrationColor(
          action?.value?.celebrationColor,
        );
        const celebrationDuration = normalizeCelebrationDuration(
          action?.value?.celebrationDurationMs,
        );
        const query = [
          `style=${encodeURIComponent(celebrationStyle)}`,
          `text=${encodeURIComponent(celebrationText)}`,
          `color=${encodeURIComponent(celebrationColor)}`,
          `durationMs=${encodeURIComponent(celebrationDuration)}`,
        ];
        await httpGet(`${joinUrl(baseUrl, "/celebrate")}?${query.join("&")}`, timeoutMs);
        await this._markOutgoingSuccess();
        if (verbosity === "verbose") {
          await this._toast(`Display: ${celebrationStyle} celebration`, 1500, true);
        }
        continue;
      }

      const message = String(
        action?.value?.message ?? action?.value?.text ?? "",
      ).trim();
      if (!message) continue;
      if (!baseUrl) {
        throw new Error("ESP Base URL is required for display actions.");
      }

      if (type === "display_message") {
        if (uiMode === "ui_post") {
          await httpPostJson(
            joinUrl(baseUrl, uiPath),
            { channel: "chat", text: message },
            timeoutMs,
          );
        } else {
          await httpGet(
            `${joinUrl(baseUrl, msgPath)}?t=${encodeURIComponent(message)}`,
            timeoutMs,
          );
        }
        await this._markOutgoingSuccess();
        if (verbosity === "verbose") {
          await this._toast("Display: message sent", 1500, true);
        }
        continue;
      }

      if (type === "status_message") {
        const statusColor = normalizeStatusColor(action?.value?.statusColor);
        const colorLabel = statusColor || "default teal";

        if (uiMode === "ui_post") {
          const payload = { channel: "status", text: message };
          if (statusColor) payload.color = statusColor;
          await httpPostJson(joinUrl(baseUrl, uiPath), payload, timeoutMs);
        } else {
          const query = [`t=${encodeURIComponent(message)}`];
          if (statusColor) {
            query.push(`color=${encodeURIComponent(statusColor)}`);
          }
          await httpGet(
            `${joinUrl(baseUrl, statusPath)}?${query.join("&")}`,
            timeoutMs,
          );
        }
        await this._markOutgoingSuccess();
        if (verbosity === "verbose") {
          await this._toast(
            `Display: status set (${colorLabel})`,
            1500,
            true,
          );
        }
      }
    }
  }

  async _handleRequest(request, response) {
    try {
      if (request.method === "GET" && request.url === "/health") {
        this._sendJson(response, 200, {
          ok: true,
          name: this.manifest?.name ?? "plugin",
          version: this.manifest?.version ?? "",
          listenerStatus: this._listenerStatus,
          listenerPort: this._getPort(),
          deviceConnected: this._deviceConnected,
        });
        return;
      }

      if (request.method !== "POST" || request.url !== "/event") {
        this._sendJson(response, 404, { ok: false, error: "Not found" });
        return;
      }

      const expectedSecret = this._getSecret();
      if (expectedSecret) {
        const receivedSecret = String(
          request.headers["x-matrix-secret"] ?? "",
        ).trim();
        if (!receivedSecret || !secretsMatch(expectedSecret, receivedSecret)) {
          this._sendJson(response, 401, {
            ok: false,
            error: "Unauthorized",
          });
          return;
        }
      }

      const body = await this._readJsonBody(request);
      await this._handleEvent(body, request, response);
    } catch (error) {
      const message = errorMessage(error);
      const statusCode =
        Number.isInteger(error?.statusCode) && error.statusCode >= 400
          ? error.statusCode
          : 500;
      await this._log(`Listener request failed: ${message}`, "error");
      this._sendJson(response, statusCode, {
        ok: false,
        error: statusCode >= 500 ? "Server error" : message,
      });
    }
  }

  async _handleEvent(body, request, response) {
    const eventNumber = parseInteger(body?.event, null);
    const sequence = parseInteger(body?.seq, null);
    const deviceId =
      typeof body?.deviceId === "string"
        ? body.deviceId.trim().slice(0, 128)
        : "";
    const heldMs = Math.max(0, parseInteger(body?.heldMs, 0));
    const rssi = parseFiniteNumber(body?.rssi, 0);
    const parsedUptimeMs = parseInteger(body?.uptimeMs, null);
    const uptimeMs =
      parsedUptimeMs === null ? null : Math.max(0, parsedUptimeMs);

    if (
      eventNumber === null ||
      eventNumber < MATRIX_EVENT_MIN ||
      eventNumber > EVENT_MAX
    ) {
      throw httpError("Invalid event number", 400);
    }
    if (sequence === null || sequence < 0) {
      throw httpError("Invalid sequence number", 400);
    }
    if (!deviceId) {
      throw httpError("Missing device ID", 400);
    }

    const ackPayload = { ok: true, seq: sequence };
    const nowMs = safeNowMs();
    const previous = this._deviceStates.get(deviceId);

    if (previous) {
      if (sequence === previous.lastSeq) {
        this._sendJson(response, 200, ackPayload);
        return;
      }
      if (sequence < previous.lastSeq) {
        const uptimeReset =
          uptimeMs !== null &&
          previous.lastUptimeMs !== null &&
          uptimeMs < previous.lastUptimeMs;
        const gapReset =
          nowMs - previous.lastEventSeenMs >= SEQ_RESET_GAP_MS;
        if (!uptimeReset && !gapReset) {
          this._sendJson(response, 200, ackPayload);
          return;
        }
      }
    }

    const inFlightKey = `${deviceId}\u0000${sequence}`;
    if (this._inFlightEvents.has(inFlightKey)) {
      this._sendJson(response, 200, ackPayload);
      return;
    }
    this._inFlightEvents.add(inFlightKey);

    try {
      const receivedAt = new Date(nowMs).toISOString();
      const remoteIp = String(request.socket?.remoteAddress ?? "").replace(
        /^::ffff:/,
        "",
      );

      this._lastSeenMs = nowMs;
      this._lastRssi = rssi;
      this._deviceId = deviceId;
      this._deviceIp = remoteIp;

      await this._setDeviceConnected(true, { silent: true });
      await this._setVariables({
        device_id: deviceId,
        device_ip: remoteIp,
        device_last_seen: receivedAt,
        device_rssi: rssi,
        seq: sequence,
        held_ms: heldMs,
      });
      await this._updateDeviceStatusText(nowMs, true);

      if (eventNumber <= MATRIX_EVENT_MAX) {
        await this._processMatrixEvent({
          eventNumber,
          sequence,
          deviceId,
          remoteIp,
          rssi,
          heldMs,
          receivedAt,
        });
      } else {
        await this._processPetEvent({
          body,
          eventNumber,
          sequence,
          deviceId,
          remoteIp,
          rssi,
          receivedAt,
        });
      }

      this._rememberDeviceState(deviceId, {
        lastSeq: sequence,
        lastUptimeMs: uptimeMs,
        lastEventSeenMs: nowMs,
      });
      this._sendJson(response, 200, ackPayload);
    } finally {
      this._inFlightEvents.delete(inFlightKey);
    }
  }

  async _processMatrixEvent(event) {
    const isLong = event.eventNumber >= KEY_COUNT;
    const keyIndex = isLong
      ? event.eventNumber - KEY_COUNT
      : event.eventNumber;
    const kind = isLong ? "long" : "short";
    const alertKey = isLong
      ? DEVICE.longAlertKey
      : DEVICE.shortAlertKey;
    const keyLabel = this._getKeyLabel(kind, keyIndex);

    await this._setVariables({
      event: keyIndex,
      kind,
      received_at: event.receivedAt,
      key_label: keyLabel,
    });

    if (this._toastVerbosity() === "verbose") {
      const label = keyLabel || `Key ${keyIndex}`;
      await this._toast(`Input: ${label} (${kind})`, 1200, true);
    }

    const payload = {
      value: String(keyIndex),
      event: keyIndex,
      kind,
      received_at: event.receivedAt,
      device_id: event.deviceId,
      device_ip: event.remoteIp,
      device_last_seen: event.receivedAt,
      device_rssi: event.rssi,
      device_connected: true,
      seq: event.sequence,
      held_ms: event.heldMs,
      key_label: keyLabel,
      device_status_text: this._lastStatusText,
    };

    await this.lumia.triggerAlert({
      alert: alertKey,
      dynamic: payload,
      extraSettings: payload,
      showInEventList: false,
    });
  }

  async _processPetEvent(event) {
    const derivedCode = event.eventNumber - PET_EVENT_BASE;
    const suppliedCode = parseInteger(event.body?.changeCode, derivedCode);
    if (suppliedCode !== derivedCode) {
      throw httpError("Pet change code does not match event number", 400);
    }

    const petCode = derivedCode;
    const changeName = String(
      event.body?.changeName || PET_NAMES[petCode] || "unknown",
    );
    const fieldId = parseInteger(event.body?.fieldId, -1);
    const fieldName = String(
      event.body?.fieldName || PET_FIELD_NAMES[fieldId] || "",
    );
    const normalizedBody = {
      ...event.body,
      changeCode: petCode,
      changeName,
      fieldId,
      fieldName,
    };

    await this._applyPetVariables(normalizedBody, true);

    if (this._toastVerbosity() === "verbose") {
      await this._toast(`Pet: ${changeName}`, 1200, true);
    }

    const payload = {
      value: changeName,
      pet_change_name: changeName,
      pet_change_code: petCode,
      pet_variation: parseFiniteNumber(event.body?.variation, petCode),
      pet_field_id: fieldId,
      pet_field_name: fieldName,
      pet_from: parseFiniteNumber(event.body?.from, 0),
      pet_to: parseFiniteNumber(event.body?.to, 0),
      pet_delta: parseFiniteNumber(event.body?.delta, 0),
      pet_ui_mode:
        typeof event.body?.uiMode === "string" ? event.body.uiMode : "",
      pet_mode_enabled: parseBoolean(event.body?.petModeEnabled, false),
      pet_alive: parseBoolean(event.body?.alive, false),
      pet_stage: parseFiniteNumber(event.body?.stage, 0),
      pet_stage_name: String(event.body?.stageName || ""),
      pet_age_minutes: parseFiniteNumber(event.body?.ageMinutes, 0),
      pet_hunger: parseFiniteNumber(event.body?.hunger, 0),
      pet_happiness: parseFiniteNumber(event.body?.happiness, 0),
      pet_energy: parseFiniteNumber(event.body?.energy, 0),
      pet_hygiene: parseFiniteNumber(event.body?.hygiene, 0),
      pet_health: parseFiniteNumber(event.body?.health, 0),
      pet_discipline: parseFiniteNumber(event.body?.discipline, 0),
      pet_poop: parseFiniteNumber(event.body?.poop, 0),
      pet_sick: parseBoolean(event.body?.sick, false),
      pet_sleeping: parseBoolean(event.body?.sleeping, false),
      pet_event_base: parseFiniteNumber(
        event.body?.petEventBase,
        PET_EVENT_BASE,
      ),
      pet_event_count: parseFiniteNumber(
        event.body?.petEventCount,
        PET_EVENT_COUNT,
      ),
      pet_queue_depth: parseFiniteNumber(event.body?.petQueueDepth, 0),
      pet_last_change_code: parseFiniteNumber(
        event.body?.lastPetEventCode,
        petCode,
      ),
      device_id: event.deviceId,
      device_ip: event.remoteIp,
      device_last_seen: event.receivedAt,
      device_rssi: event.rssi,
      device_connected: true,
      seq: event.sequence,
      device_status_text: this._lastStatusText,
    };

    await this.lumia.triggerAlert({
      alert: "pet",
      dynamic: payload,
      extraSettings: payload,
      showInEventList: false,
    });
  }

  _readJsonBody(request) {
    return new Promise((resolve, reject) => {
      let data = "";
      let byteLength = 0;
      let tooLarge = false;

      request.on("data", (chunk) => {
        byteLength += chunk.length;
        if (byteLength > MAX_BODY_BYTES) {
          tooLarge = true;
          data = "";
          return;
        }
        if (!tooLarge) data += chunk.toString("utf8");
      });

      request.on("end", () => {
        if (tooLarge) {
          reject(httpError("Request body too large", 413));
          return;
        }
        if (!data) {
          reject(httpError("Missing JSON body", 400));
          return;
        }
        try {
          const parsed = JSON.parse(data);
          if (!parsed || typeof parsed !== "object" || Array.isArray(parsed)) {
            reject(httpError("JSON body must be an object", 400));
            return;
          }
          resolve(parsed);
        } catch {
          reject(httpError("Invalid JSON body", 400));
        }
      });

      request.on("error", reject);
    });
  }

  _sendJson(response, status, value) {
    if (!response || response.destroyed || response.writableEnded) return;
    try {
      const payload = JSON.stringify(value);
      if (!response.headersSent) {
        response.writeHead(status, {
          "Content-Type": "application/json",
          "Content-Length": Buffer.byteLength(payload),
          "Cache-Control": "no-store",
        });
      }
      response.end(payload);
    } catch {
      try {
        if (!response.headersSent) response.writeHead(500);
        response.end();
      } catch {}
    }
  }
};
