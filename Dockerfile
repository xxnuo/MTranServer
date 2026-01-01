# Build stage
FROM oven/bun:1 AS builder
WORKDIR /app

# Argument for version
ARG VERSION
ENV VERSION=${VERSION}

# Copy package files (Root)
COPY package.json bun.lock ./
RUN bun install --frozen-lockfile

# Copy package files (UI) - to leverage caching
WORKDIR /app/ui
COPY ui/package.json ui/bun.lock ./
RUN bun install --frozen-lockfile

# Copy source code
WORKDIR /app
COPY . .

# Inject version if provided
RUN if [ -n "$VERSION" ]; then \
    sed -i "s/export const VERSION = '.*';/export const VERSION = '$VERSION';/" src/version/index.ts; \
    fi

# Build All (UI, Assets, Spec, Binaries)
RUN bun run build:docker

# Final stage
FROM alpine:latest

# Install runtime dependencies
RUN apk add --no-cache libstdc++ ca-certificates

WORKDIR /app

# Copy all built binaries
COPY --from=builder /app/dist ./dist

ARG BUILD_VARIANT

# Select the correct binary based on the container's architecture
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

# Set environment variables
ENV MT_HOST=0.0.0.0 \
    MT_PORT=8989 \
    NODE_ENV=production

# Expose the port
EXPOSE 8989

# Run the server
CMD ["./mtranserver"]
