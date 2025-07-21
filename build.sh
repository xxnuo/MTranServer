#!/bin/sh

if [ $# -eq 0 ]; then
  echo "错误: 请提供版本号"
  echo "用法: $0 <版本号>"
  exit 1
fi

VERSION=$1
echo "开始构建版本: $VERSION"

# 构建标准版镜像（不预加载模型）
echo "构建标准版镜像 mtranserver:$VERSION..."
docker build -t xxnuo/mtranserver:$VERSION \
    -t xxnuo/mtranserver:latest \
    -f Dockerfile .

# 构建中文预加载版镜像
echo "构建中文预加载版镜像 mtranserver:${VERSION}-zh..."
docker build -t xxnuo/mtranserver:${VERSION}-zh \
    -t xxnuo/mtranserver:latest-zh \
    --build-arg PRELOAD_SRC_LANG=zh-Hans \
    --build-arg PRELOAD_TARGET_LANG=en \
    -f Dockerfile.model .

# 构建日语预加载版镜像
echo "构建日语预加载版镜像 mtranserver:${VERSION}-ja..."
docker build -t xxnuo/mtranserver:${VERSION}-ja \
    -t xxnuo/mtranserver:latest-ja \
    --build-arg PRELOAD_SRC_LANG=ja \
    --build-arg PRELOAD_TARGET_LANG=en \
    -f Dockerfile.model .

echo "构建完成!"