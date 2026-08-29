FROM node:22-alpine AS build

WORKDIR /app

COPY frontend/package.json frontend/package-lock.json ./
RUN npm ci

ARG VITE_JSB1_BRANCH=dev
ARG VITE_JSB1_COMMIT=unknown
ARG VITE_JSB1_BUILD_TIME=unknown
ARG VITE_JSB1_HOSTNAME=
ENV VITE_JSB1_BRANCH=${VITE_JSB1_BRANCH} \
    VITE_JSB1_COMMIT=${VITE_JSB1_COMMIT} \
    VITE_JSB1_BUILD_TIME=${VITE_JSB1_BUILD_TIME} \
    VITE_JSB1_HOSTNAME=${VITE_JSB1_HOSTNAME}

COPY frontend/ ./
RUN npm run build

FROM caddy:2.10-alpine

COPY --from=deploy-config Caddyfile.app /etc/caddy/Caddyfile
COPY --from=build /app/dist /usr/share/caddy

EXPOSE 8080
