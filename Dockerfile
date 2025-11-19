# Build stage
FROM golang:1.25.3-alpine AS builder

# Install build dependencies
RUN apk add --no-cache git make

WORKDIR /build

# Copy go mod files
COPY go.mod go.sum ./
RUN go mod download

# Copy source code
COPY . .

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
