ARG DEBIAN_MIRROR=http://mirrors.ustc.edu.cn

FROM oven/bun:1 AS builder
WORKDIR /app

ARG VERSION
ENV VERSION=${VERSION}

COPY package.json bun.lock ./
COPY scripts/install-runtime.mjs ./scripts/install-runtime.mjs
RUN --mount=type=cache,target=/root/.bun/install/cache \
    bun install --frozen-lockfile

COPY ui/package.json ui/bun.lock ./ui/
RUN --mount=type=cache,target=/root/.bun/install/cache \
    cd ui && bun install --frozen-lockfile

COPY . .

RUN if [ -z "$VERSION" ]; then VERSION=$(bun -p "require('./package.json').version"); fi; \
    bun run bump "$VERSION"

RUN --mount=type=cache,target=/root/.bun/install/cache \
    bun run build:node

RUN --mount=type=cache,target=/root/.bun/install/cache \
    mkdir /runtime && cp package.json bun.lock /runtime/ && \
    cd /runtime && bun install --production --frozen-lockfile --ignore-scripts

FROM node:22-bookworm AS native-builder
ARG DEBIAN_MIRROR
RUN sed -i "s|http://deb.debian.org|${DEBIAN_MIRROR}|g" /etc/apt/sources.list.d/debian.sources
WORKDIR /native
COPY --from=builder /usr/local/bin/bun /usr/local/bin/bun
COPY scripts/build-native.ts ./scripts/build-native.ts
COPY native/onnx-async ./native/onnx-async
COPY native/ssru ./native/ssru
RUN bun scripts/build-native.ts && \
    node -e 'const a=require("./dist/onnx-async-linux-"+process.arch+".node"); if(a.ssruVersion!==1) process.exit(1)'

FROM node:22-bookworm-slim AS runtime
ARG DEBIAN_MIRROR

RUN sed -i "s|http://deb.debian.org|${DEBIAN_MIRROR}|g" /etc/apt/sources.list.d/debian.sources \
    && apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates libtcmalloc-minimal4 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY --from=builder /app/dist ./
COPY --from=native-builder /native/dist/*.node ./
COPY --from=builder /runtime/node_modules ./node_modules
COPY --from=builder /runtime/package.json ./package.json

ENV MT_HOST=0.0.0.0 \
    MT_PORT=8989 \
    MT_FAST=true \
    NODE_ENV=production \
    LD_PRELOAD=libtcmalloc_minimal.so.4 \
    TCMALLOC_AGGRESSIVE_DECOMMIT=1 \
    TCMALLOC_RELEASE_RATE=10

EXPOSE 8989

CMD ["node", "main.js"]

FROM runtime AS webgpu
RUN apt-get update && apt-get install -y --no-install-recommends \
    libvulkan1 mesa-vulkan-drivers \
    && rm -rf /var/lib/apt/lists/*

FROM runtime AS cpu
