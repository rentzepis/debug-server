const fs = require("fs");
const http = require("http");
const { createCheckinStore } = require("../checkin/store");

const CONTAINER_PREFIX = "code-";

/**
 * Decide which student containers to start or stop.
 * @param {{name: string, running: boolean}[]} containers code-* containers
 * @param {(username: string) => boolean} shouldRun
 */
function plan(containers, shouldRun) {
  const start = [];
  const stop = [];
  for (const { name, running } of containers) {
    const wanted = shouldRun(name.slice(CONTAINER_PREFIX.length));
    if (wanted && !running) {
      start.push(name);
    } else if (!wanted && running) {
      stop.push(name);
    }
  }
  return { start, stop };
}

/** Same rule the gateway uses to unlock a workspace: staff, or checked in and not finished. */
function createRunPolicy({ users, staff, store }) {
  const staffLower = new Set(staff.map((s) => s.toLowerCase()));
  return (username) =>
    staffLower.has(username.toLowerCase()) ||
    (Object.prototype.hasOwnProperty.call(users, username) && store.isUnlocked(username));
}

function readJson(file, fallback) {
  try {
    return JSON.parse(fs.readFileSync(file, "utf8") || "null") ?? fallback;
  } catch {
    return fallback;
  }
}

function docker(socketPath, method, path) {
  return new Promise((resolve, reject) => {
    const req = http.request({ socketPath, method, path }, (res) => {
      const chunks = [];
      res.on("data", (chunk) => chunks.push(chunk));
      res.on("end", () => {
        const body = Buffer.concat(chunks).toString("utf8");
        // 304: already started / already stopped.
        if (res.statusCode >= 400) {
          reject(new Error(`docker ${method} ${path}: ${res.statusCode} ${body.trim()}`));
          return;
        }
        resolve(body ? JSON.parse(body) : null);
      });
    });
    req.on("error", reject);
    req.end();
  });
}

async function listStudentContainers(socketPath) {
  const filters = encodeURIComponent(JSON.stringify({ name: [`^/${CONTAINER_PREFIX}`] }));
  const rows = await docker(socketPath, "GET", `/containers/json?all=1&filters=${filters}`);
  return rows
    .map((row) => ({
      name: String((row.Names || [])[0] || "").replace(/^\//, ""),
      running: row.State === "running",
    }))
    .filter((c) => c.name.startsWith(CONTAINER_PREFIX));
}

function log(event, fields) {
  console.log(JSON.stringify({ ts: new Date().toISOString(), event, ...fields }));
}

function main() {
  const socketPath = process.env.DOCKER_SOCKET || "/var/run/docker.sock";
  const usersFile = process.env.USERS_FILE || "/data/users.json";
  const staffFile = process.env.STAFF_FILE || "/data/staff.json";
  const stateFile = process.env.CHECKIN_STATE_FILE || "/data/state/checkins.json";
  const intervalMs = Number(process.env.RECONCILE_INTERVAL_MS || 2000);
  const stopTimeoutSec = Number(process.env.STOP_TIMEOUT_SECONDS || 5);

  async function tick() {
    const staff = readJson(staffFile, []);
    const shouldRun = createRunPolicy({
      users: readJson(usersFile, {}),
      staff: Array.isArray(staff) ? staff.filter((s) => typeof s === "string") : [],
      store: createCheckinStore({ file: stateFile }),
    });
    const { start, stop } = plan(await listStudentContainers(socketPath), shouldRun);
    // In parallel so closing a whole section waits one stop timeout, not one per student.
    await Promise.all([
      ...start.map((name) =>
        docker(socketPath, "POST", `/containers/${name}/start`).then(
          () => log("container.started", { container: name }),
          (err) => log("container.start_failed", { container: name, error: err.message }),
        ),
      ),
      ...stop.map((name) =>
        docker(socketPath, "POST", `/containers/${name}/stop?t=${stopTimeoutSec}`).then(
          () => log("container.stopped", { container: name }),
          (err) => log("container.stop_failed", { container: name, error: err.message }),
        ),
      ),
    ]);
  }

  async function loop() {
    try {
      await tick();
    } catch (err) {
      log("reconcile.failed", { error: err.message });
    }
    setTimeout(loop, intervalMs);
  }

  log("reconcile.started", { intervalMs });
  loop();
}

if (require.main === module) {
  main();
}

module.exports = { plan, createRunPolicy, docker, listStudentContainers };
