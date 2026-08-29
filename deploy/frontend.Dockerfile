FROM node:22-alpine AS build

WORKDIR /app

COPY frontend/package.json frontend/package-lock.json ./
RUN npm ci

COPY frontend/ ./
RUN npm run build

FROM caddy:2.10-alpine

COPY --from=deploy-config Caddyfile.app /etc/caddy/Caddyfile
COPY --from=build /app/dist /usr/share/caddy

EXPOSE 8080
