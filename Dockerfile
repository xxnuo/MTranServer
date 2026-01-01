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

FROM node:22-alpine

WORKDIR /app

COPY --from=builder /app/dist ./
COPY --from=builder /app/node_modules ./node_modules

ENV MT_HOST=0.0.0.0 \
    MT_PORT=8989 \
    NODE_ENV=production

EXPOSE 8989

CMD ["node", "main.js"]
