FROM debian:bookworm-slim

# 设置工作目录
WORKDIR /app

# 复制可执行文件
COPY core /app/core
RUN chmod +x /app/core

RUN mkdir -p /app/models/

# 暴露服务端口
EXPOSE 8989

# 启动服务
CMD ["/app/core"]