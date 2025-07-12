#!/bin/sh

# 测试运行脚本

# 获取脚本所在目录的绝对路径
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)

# 切换到脚本所在目录
cd "$SCRIPT_DIR" || exit 1

# 设置环境变量
export HOST=${HOST:-localhost}
export PORT=${PORT:-8989}
export CORE_API_TOKEN=${CORE_API_TOKEN:-}

# 显示测试环境
echo "测试环境:"
echo "HOST: $HOST"
echo "PORT: $PORT"
echo "TOKEN: ${CORE_API_TOKEN:-(未设置)}"
echo ""

# 运行测试
echo "=== 运行核心API测试 ==="
node tests/core-api.js
echo ""

echo "=== 运行翻译API测试 ==="
node tests/translate-api.js
echo ""

echo "=== 运行翻译插件兼容API测试 ==="
node tests/plugin-api.js
echo ""

echo "所有测试完成!" 