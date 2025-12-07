# Build stage
FROM golang:1.25.3-bookworm AS builder

# Install build dependencies
RUN apt-get update && \
    apt-get install -y --no-install-recommends git make curl && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /build

# Copy go mod files
COPY go.mod go.sum ./
RUN go mod download

# Copy source code (UI dist should be pre-built and copied)
COPY . .

# Download resources
RUN make download

# Verify required files exist (should be prepared before docker build)
RUN echo "Verifying build prerequisites..." && \
    if [ ! -f "ui/dist/index.html" ]; then \
        echo "ERROR: ui/dist/index.html not found!"; \
        exit 1; \
    fi && \
    if [ ! -f "bin/worker" ]; then \
        echo "ERROR: bin/worker not found!"; \
        exit 1; \
    fi && \
    if [ ! -f "internal/docs/swagger.json" ]; then \
        echo "ERROR: internal/docs/swagger.json not found!"; \
        exit 1; \
    fi && \
    echo "✓ All prerequisites verified"

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

WORKDIR /app

# Copy binary from builder
COPY --from=builder /build/mtranserver /app/mtranserver

# Create directories for data and models
RUN mkdir -p /app/data /app/models && \

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
