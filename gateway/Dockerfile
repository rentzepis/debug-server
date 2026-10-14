FROM node:24-bookworm-slim

WORKDIR /app

COPY package*.json ./
RUN npm install --omit=dev && npm cache clean --force

COPY server.js ./
COPY checkin/ ./checkin/
COPY public/ ./public/

ENV PORT=8080 \
    USERS_FILE=/data/users.json \
    STAFF_FILE=/data/staff.json \
    CHECKIN_STATE_FILE=/data/state/checkins.json

EXPOSE 8080

CMD ["node", "server.js"]
