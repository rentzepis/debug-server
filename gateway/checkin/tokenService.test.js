const assert = require("node:assert/strict");
const crypto = require("node:crypto");
const { test } = require("node:test");
const {
  CHECKIN_QR_PAYLOAD_PREFIX,
  PASSCODE_PERIOD_MS,
  TOKEN_PERIOD_MS,
} = require("./constants");
const { createCheckinTokenService } = require("./tokenService");

const secret = "checkin-protocol-test-secret";

function fixture(timestamp = 20_000) {
  let time = timestamp;
  const service = createCheckinTokenService({
    getSecret: () => secret,
    now: () => time,
  });
  return {
    service,
    setTime: (value) => {
      time = value;
    },
  };
}

function expectedToken(scope, bucket) {
  return crypto
    .createHmac("sha256", secret)
    .update(`${scope}:${bucket}`)
    .digest("hex")
    .slice(0, 24);
}

test("wire prefix and periods", () => {
  assert.equal(CHECKIN_QR_PAYLOAD_PREFIX, "debug-server-checkin:");
  assert.equal(TOKEN_PERIOD_MS, 2_000);
  assert.equal(PASSCODE_PERIOD_MS, 5_000);
});

test("issuance uses the HMAC scope, truncation and expiry boundaries", () => {
  const { service, setTime } = fixture(21_999);
  assert.deepEqual(service.getCurrentToken("lab1", "alice"), {
    token: expectedToken("checkin-qr:lab1:alice", 10),
    periodMs: 2_000,
    expiresAt: 22_000,
  });
  setTime(22_000);
  assert.equal(
    service.getCurrentToken("lab1", "alice").token,
    expectedToken("checkin-qr:lab1:alice", 11),
  );
  assert.match(service.getCurrentToken("lab1", "alice").token, /^[a-f0-9]{24}$/u);
});

test("validators accept only current and adjacent buckets in the right scope", () => {
  const { service } = fixture();
  for (const bucket of [9, 10, 11]) {
    assert.equal(
      service.isValidToken("lab1", "alice", expectedToken("checkin-qr:lab1:alice", bucket)),
      true,
    );
  }
  for (const bucket of [8, 12]) {
    assert.equal(
      service.isValidToken("lab1", "alice", expectedToken("checkin-qr:lab1:alice", bucket)),
      false,
    );
  }
  const token = expectedToken("checkin-qr:lab1:alice", 10);
  assert.equal(service.isValidToken("lab1", "bob", token), false);
  assert.equal(service.isValidToken("lab2", "alice", token), false);
  assert.equal(service.isValidToken("lab1", "alice", ""), false);
  assert.equal(service.isValidToken("lab1", "alice", undefined), false);
  assert.equal(service.isValidPasscode("lab1", token), false);
});

test("tokens expire after the adjacent-bucket grace period", () => {
  const { service, setTime } = fixture();
  const { token } = service.getCurrentToken("lab1", "alice");
  setTime(23_999);
  assert.equal(service.isValidToken("lab1", "alice", token), true);
  setTime(24_000);
  assert.equal(service.isValidToken("lab1", "alice", token), false);
});

test("passcodes are four digits, assignment-scoped and process-local with inclusive expiry", () => {
  const { service, setTime } = fixture();
  const { service: anotherProcess } = fixture();
  const issued = service.issuePasscode("lab1");
  assert.match(issued.passcode, /^\d{4}$/u);
  assert.equal(issued.periodMs, 5_000);
  assert.equal(issued.expiresAt, 25_000);
  assert.equal(service.isValidPasscode("lab1", issued.passcode), true);
  assert.equal(service.isValidPasscode("lab2", issued.passcode), false);
  assert.equal(anotherProcess.isValidPasscode("lab1", issued.passcode), false);
  for (const invalid of ["", "123", "12345", "abcd", `${issued.passcode} `, 1234]) {
    assert.equal(service.isValidPasscode("lab1", invalid), false);
  }
  setTime(25_000);
  assert.equal(service.isValidPasscode("lab1", issued.passcode), true);
  setTime(25_001);
  assert.equal(service.isValidPasscode("lab1", issued.passcode), false);
});

test("reissuing a passcode replaces its expiration and expired entries are removed", () => {
  const { service, setTime } = fixture();
  service.issuePasscode("lab1");
  setTime(24_000);
  const latest = service.issuePasscode("lab1");
  assert.equal(latest.expiresAt, 29_000);
  setTime(25_001);
  assert.equal(service.isValidPasscode("lab1", latest.passcode), true);
  setTime(29_001);
  assert.equal(service.isValidPasscode("lab1", latest.passcode), false);
  setTime(24_000);
  assert.equal(service.isValidPasscode("lab1", latest.passcode), false);
});

test("secret resolution stays lazy and errors propagate to the caller", () => {
  const service = createCheckinTokenService({
    getSecret: () => {
      throw new Error("missing secret");
    },
    now: () => 20_000,
  });
  assert.throws(() => service.getCurrentToken("lab1", "alice"), /missing secret/u);
  assert.throws(() => service.isValidToken("lab1", "alice", "token"), /missing secret/u);
});
