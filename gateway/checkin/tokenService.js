const crypto = require("crypto");
const { PASSCODE_PERIOD_MS, TOKEN_PERIOD_MS } = require("./constants");

// Server-only protocol core. Each service owns its process-local passcode store.
function createCheckinTokenService({ getSecret, now = Date.now }) {
  const activePasscodesByAssignment = new Map();

  function scopeFor(assignmentId, username) {
    return `checkin-qr:${assignmentId}:${username}`;
  }

  function computeTokenForBucket(scope, bucket) {
    return crypto
      .createHmac("sha256", getSecret())
      .update(`${scope}:${String(bucket)}`)
      .digest("hex")
      .slice(0, 24);
  }

  function currentBucket() {
    return Math.floor(now() / TOKEN_PERIOD_MS);
  }

  return {
    getCurrentToken(assignmentId, username) {
      const bucket = currentBucket();
      return {
        token: computeTokenForBucket(scopeFor(assignmentId, username), bucket),
        periodMs: TOKEN_PERIOD_MS,
        expiresAt: (bucket + 1) * TOKEN_PERIOD_MS,
      };
    },

    isValidToken(assignmentId, username, token) {
      if (typeof token !== "string" || !token) {
        return false;
      }
      const scope = scopeFor(assignmentId, username);
      const bucket = currentBucket();
      return [bucket - 1, bucket, bucket + 1].some((candidate) => {
        const expected = Buffer.from(computeTokenForBucket(scope, candidate));
        const actual = Buffer.from(token);
        return (
          expected.length === actual.length &&
          crypto.timingSafeEqual(expected, actual)
        );
      });
    },

    issuePasscode(assignmentId) {
      const passcode = crypto.randomInt(0, 10_000).toString().padStart(4, "0");
      const expiresAt = now() + PASSCODE_PERIOD_MS;
      activePasscodesByAssignment.set(assignmentId, { passcode, expiresAt });
      return { passcode, periodMs: PASSCODE_PERIOD_MS, expiresAt };
    },

    isValidPasscode(assignmentId, passcode) {
      if (typeof passcode !== "string" || !/^\d{4}$/u.test(passcode)) {
        return false;
      }
      const active = activePasscodesByAssignment.get(assignmentId);
      if (!active) {
        return false;
      }
      if (now() > active.expiresAt) {
        activePasscodesByAssignment.delete(assignmentId);
        return false;
      }
      return active.passcode === passcode;
    },
  };
}

module.exports = { createCheckinTokenService };
