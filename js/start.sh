#!/bin/sh

# 翻译服务器启动脚本

# 获取脚本所在目录的绝对路径
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)

# 切换到脚本所在目录
cd "$SCRIPT_DIR" || exit 1

# 检查是否设置了环境变量
if [ -z "$PORT" ]; then
  export PORT=8989
fi

if [ -z "$HOST" ]; then
  export HOST=0.0.0.0
fi

# 启动服务
echo "Starting MTranServer..."
node js/mts.js

# 脚本结束
exit 0 