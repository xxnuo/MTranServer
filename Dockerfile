# Build stage
FROM golang:1.25.3-bookworm AS builder

# Install build dependencies
RUN apt-get update && \
    apt-get install -y --no-install-recommends git make curl && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*

# Install Node.js and pnpm (skip on unsupported architectures)
RUN ARCH=$(dpkg --print-architecture) && \
    if [ "$ARCH" = "amd64" ] || [ "$ARCH" = "arm64" ]; then \
        curl -fsSL https://deb.nodesource.com/setup_20.x | bash - && \
        apt-get install -y --no-install-recommends nodejs && \
        npm install -g corepack && \
        corepack enable && \
        apt-get clean && \
        rm -rf /var/lib/apt/lists/*; \
    else \
        echo "Skipping Node.js installation on unsupported architecture: $ARCH"; \
    fi

WORKDIR /build

# Copy go mod files
COPY go.mod go.sum ./
RUN go mod download

# Copy source code
COPY . .

# Build UI (skip on unsupported platforms like linux/386)
RUN cd ui && \
    (corepack enable && pnpm install --frozen-lockfile && pnpm build || \
     (echo "UI build failed or unsupported platform, creating empty dist" && mkdir -p dist && echo '<!DOCTYPE html><html><body>UI not available</body></html>' > dist/index.html))

# Download resources
RUN make download

# Generate docs
RUN make generate-docs

# Build binary
ARG VERSION=dev
RUN CGO_ENABLED=0 GOOS=linux go build \
    -ldflags "-X github.com/xxnuo/MTranServer/internal/version.Version=${VERSION} -s -w" \
    -o mtranserver \
    ./cmd/mtranserver

# Runtime stage
FROM alpine:latest

# Install runtime dependencies
RUN apk add --no-cache ca-certificates tzdata curl

# Create non-root user
RUN addgroup -g 1000 mtran && \
    adduser -D -u 1000 -G mtran mtran

WORKDIR /app

# Copy binary from builder
COPY --from=builder /build/mtranserver /app/mtranserver

# Create directories for data and models
RUN mkdir -p /app/data /app/models && \
    chown -R mtran:mtran /app

# Switch to non-root user
USER mtran

# Expose port
EXPOSE 8989

# Health check
HEALTHCHECK --interval=30s --timeout=3s --start-period=5s --retries=3 \
    CMD curl -f http://localhost:8989/health || exit 1

# Set default environment variables
ENV MT_HOST=0.0.0.0 \
    MT_PORT=8989 \
    MT_LOG_LEVEL=warn \
    MT_CONFIG_DIR=/app/data \
    MT_MODEL_DIR=/app/models \
    MT_ENABLE_UI=true \
    MT_OFFLINE=false

# Run the application
CMD ["/app/mtranserver"]
