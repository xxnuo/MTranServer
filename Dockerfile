# Use the official Bun image
# See https://bun.com/docs/guides/ecosystem/docker
FROM oven/bun:1 AS base
WORKDIR /app

# Stage 1: Build UI
FROM base AS ui-builder
WORKDIR /app/ui
COPY ui/package.json ui/bun.lock ./
# Install dependencies using bun
RUN bun installCOPY ui/ .
# Build UI
RUN bun run build

# Stage 2: Install Server Dependencies
FROM base AS install
WORKDIR /app
COPY package.json bun.lock ./
# Install production dependencies
RUN bun install --frozen-lockfile --production

# Stage 3: Final Image
FROM base AS release
WORKDIR /app

# Argument for version
ARG VERSION
ENV VERSION=${VERSION}

# Copy production dependencies
COPY --from=install /app/node_modules ./node_modules
COPY package.json bun.lock ./

# Copy source code
COPY src ./src
COPY scripts ./scripts
COPY tsconfig.json ./

# Inject version if provided
# We use a simple sed command to replace the version in the source file
RUN if [ -n "$VERSION" ]; then \
    sed -i "s/export const VERSION = '.*';/export const VERSION = '$VERSION';/" src/version/index.ts; \
    fi

# Copy built UI assets
COPY --from=ui-builder /app/ui/dist ./ui/dist

# Expose port
EXPOSE 8989

# Environment variables
ENV MT_HOST=0.0.0.0 \
    MT_PORT=8989 \
    NODE_ENV=production

# Command to run the server
CMD ["bun", "src/main.ts"]
