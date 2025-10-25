FROM node:lts-alpine AS builder

WORKDIR /app
COPY package.json package.json
COPY packages/ ./packages/
RUN npm install

FROM alpine:latest

RUN apk update && \
    apk add --no-cache nodejs curl

WORKDIR /app

COPY --from=builder /app/node_modules ./node_modules

COPY js ./js
COPY package.json ./package.json
COPY start.sh ./start.sh
RUN chmod +x ./start.sh

EXPOSE 8989

ENV NODE_ENV=production
ENV HOST=0.0.0.0
ENV PORT=8989


CMD ["./start.sh"]
