const CHECKIN_QR_PAYLOAD_PREFIX = "debug-server-checkin:";
const TOKEN_PERIOD_MS = 2_000;
const PASSCODE_PERIOD_MS = 5_000;

const ASSIGNMENT_ID_PATTERN = /^[a-z0-9](?:[a-z0-9-]{0,62}[a-z0-9])?$/u;

module.exports = {
  CHECKIN_QR_PAYLOAD_PREFIX,
  TOKEN_PERIOD_MS,
  PASSCODE_PERIOD_MS,
  ASSIGNMENT_ID_PATTERN,
};
