FROM golang:1.24rc3-bookworm

# 使用阿里云镜像源
RUN sed -i 's/deb.debian.org/mirrors.ustc.edu.cn/g' /etc/apt/sources.list.d/debian.sources

RUN apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    curl \
    wget \
    git \
    make \
    vim

WORKDIR /app

# 设置Go模块相关环境变量
ENV GO111MODULE=on
ENV GOPROXY=https://goproxy.cn,direct

# 配置Warp终端
RUN echo 'printf '\''\eP$f{"hook": "SourcedRcFileForWarp", "value": { "shell": "bash"}}\x9c'\'' ' >> /root/.bashrc

CMD ["/bin/bash"]