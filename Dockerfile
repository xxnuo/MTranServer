FROM oven/bun:1 AS builder
WORKDIR /app

ARG VERSION
ENV VERSION=${VERSION}

WORKDIR /app
COPY . .

RUN bun install --frozen-lockfile
RUN cd ui && bun install --frozen-lockfile

RUN if [ -n "$VERSION" ]; then bun run bump $VERSION; fi

RUN bun run build:docker

FROM alpine:latest

RUN apk add --no-cache libstdc++ ca-certificates

WORKDIR /app

COPY --from=builder /app/dist ./dist

ARG BUILD_VARIANT

RUN ARCH=$(uname -m) && \
    case "$ARCH" in \
      x86_64) \
        if [ "$BUILD_VARIANT" = "legacy" ]; then \
          mv dist/*-linux-amd64-musl-legacy ./mtranserver; \
        else \
          mv dist/*-linux-amd64-musl ./mtranserver; \
        fi ;; \
      aarch64) mv dist/*-linux-arm64-musl ./mtranserver ;; \
      *) echo "Unsupported architecture: $ARCH"; exit 1 ;; \
    esac && \
    chmod +x ./mtranserver && \
    rm -rf dist

ENV MT_HOST=0.0.0.0 \
    MT_PORT=8989 \
    NODE_ENV=production

EXPOSE 8989

CMD ["./mtranserver"]
