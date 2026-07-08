FROM node:24-bookworm-slim

WORKDIR /app

COPY package.json server.js ./
RUN npm install --omit=dev && npm cache clean --force

ENV PORT=8080 \
    USERS_FILE=/data/users.json

EXPOSE 8080

CMD ["node", "server.js"]
