FROM debian:bookworm-slim

# 设置工作目录
WORKDIR /app

ARG DEBIAN_FRONTEND=noninteractive

# RUN apt update 
# RUN apt install -y gpg-agent wget
# RUN wget -O- https://apt.repos.intel.com/intel-gpg-keys/GPG-PUB-KEY-INTEL-SW-PRODUCTS.PUB | gpg --dearmor | tee /usr/share/keyrings/oneapi-archive-keyring.gpg > /dev/null
# RUN echo "deb [signed-by=/usr/share/keyrings/oneapi-archive-keyring.gpg] https://apt.repos.intel.com/oneapi all main" | tee /etc/apt/sources.list.d/oneAPI.list
# RUN apt update
# RUN apt install -y intel-oneapi-mkl

# 复制可执行文件
COPY core /app/core
RUN chmod +x /app/core

RUN mkdir -p /app/models/

# 暴露服务端口
EXPOSE 8989

# 启动服务
CMD ["/app/core"]