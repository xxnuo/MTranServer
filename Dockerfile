FROM oven/bun:1 AS builder
WORKDIR /app

ARG VERSION
ENV VERSION=${VERSION}

COPY package.json bun.lock ./
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
WORKDIR /native
COPY native/onnx-async ./
RUN c++ -std=c++20 -O3 -shared -fPIC -pthread -I/usr/local/include/node \
    addon.cc -ldl -o "onnx-async-linux-$(node -p process.arch).node"

FROM node:22-bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates libvulkan1 mesa-vulkan-drivers \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY --from=builder /app/dist ./
COPY --from=native-builder /native/*.node ./
COPY --from=builder /runtime/node_modules ./node_modules
COPY --from=builder /runtime/package.json ./package.json

ENV MT_HOST=0.0.0.0 \
    MT_PORT=8989 \
    NODE_ENV=production

EXPOSE 8989

CMD ["node", "main.js"]
