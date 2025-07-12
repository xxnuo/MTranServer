FROM node:lts-alpine AS builder

WORKDIR /app
COPY package.json package.json
COPY packages/ ./packages/
# RUN npm install --registry=https://registry.npmmirror.com
RUN npm install

COPY js/ ./js/
COPY start.sh ./
RUN chmod +x start.sh

FROM alpine:latest

# RUN sed -i 's/dl-cdn.alpinelinux.org/mirrors.tuna.tsinghua.edu.cn/g' /etc/apk/repositories && \
RUN apk update && \
    apk add --no-cache nodejs curl

WORKDIR /app

COPY --from=builder /app/js ./js
COPY --from=builder /app/node_modules ./node_modules
COPY --from=builder /app/start.sh ./
RUN chmod +x start.sh

EXPOSE 8989

ENV NODE_ENV=production
ENV HOST=0.0.0.0
ENV PORT=8989

CMD ["./start.sh"]
