# MTranServer Linux 部署文档

MTranServer 是离线翻译服务器（Bergamot/WASM 引擎），含 OCR 图片翻译功能。本文档覆盖 Linux 裸机部署的完整流程，不依赖 Docker。

## 环境要求

| 项目 | 要求 |
|------|------|
| OS | Linux x86_64 / arm64（Ubuntu、Debian、CentOS、Alpine 均可） |
| Node.js | ≥ 18（运行时必需） |
| Bun | ≥ 1.2（仅源码构建时需要，运行时不需要） |
| 内存 | ≥ 512MB（每个语言对约占用 200-400MB） |
| 磁盘 | ≥ 2GB（模型 + 程序） |

## 部署方式

三种方式按场景选择：

- **源码构建**：改过源码或需要自定义功能时用（本项目当前场景）
- **npm 全局安装**：最省事，不改源码时用
- **单文件二进制**：零依赖，单文件分发

---

### 方式一：源码构建部署（推荐）

适合当前项目（含自定义 OCR 功能），构建产物用 Node.js 运行。

#### 1. 安装依赖

```bash
# Node.js 22.x
curl -fsSL https://deb.nodesource.com/setup_22.x | sudo bash -
sudo apt-get install -y nodejs

# Bun（仅构建用）
curl -fsSL https://bun.sh/install | bash
source ~/.bashrc
```

#### 2. 构建产物

```bash
git clone <仓库地址> /opt/mtranserver
cd /opt/mtranserver

# 安装依赖
bun install
cd ui && bun install && cd ..

# 构建前端 UI
cd ui && bun run build && cd ..
bun run scripts/gen-ui-assets.ts
bun run scripts/gen-swagger-assets.ts

# 生成路由和 API 文档
bun tsoa spec-and-routes

# 构建后端生产产物（产出 dist/main.js，ESM 格式，含全部资源）
bun run build:node
```

构建后 `dist/` 目录包含运行所需的全部文件（UI、Swagger、WASM、字体），可直接拷贝到任意有 Node.js 的服务器运行。

#### 3. 启动验证

```bash
node dist/main.js --host 0.0.0.0 --port 8989 --model-dir /opt/mtranserver/models --log-level info
```

看到 `MTranServer v4.0.33 is running!` 即启动成功。

---

### 方式二：npm 全局安装（最省事）

不改源码时用，一行命令拉起。

```bash
# 全局安装
npm i -g mtranserver@latest

# 直接启动
mtranserver --host 0.0.0.0 --port 8989

# 或临时运行不安装
npx mtranserver@latest --host 0.0.0.0 --port 8989
```

---

### 方式三：单文件二进制（零依赖）

构建出单个可执行文件，自带 Bun 运行时，目标机器无需装任何东西。

```bash
cd /opt/mtranserver
bun run build --single

# 产物在 dist/mtranserver，直接运行
./dist/mtranserver --host 0.0.0.0 --port 8989 --model-dir /opt/mtranserver/models
```

---

## 配置说明

配置优先级：**命令行参数 > 环境变量 > 配置文件 > 默认值**。

### 命令行参数

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `--host` | `0.0.0.0` | 监听地址 |
| `--port` | `8989` | 监听端口 |
| `--model-dir` | `~/.config/mtran/models` | 模型目录 |
| `--api-token` | 空 | API 访问令牌，设置后请求需带 `Authorization: Bearer <token>` |
| `--offline` | `false` | 离线模式，禁止下载模型 |
| `--log-level` | `warn` | 日志级别：debug/info/warn/error |
| `--model-source` | `mirror` | 模型源：`mirror`（镜像站）或 `official`（官方） |
| `--model-mirror-url` | `http://183.136.206.212:8787` | 镜像下载站地址 |
| `--download-proxy` | 空 | 模型下载代理（HTTP/SOCKS） |
| `--worker-idle-timeout` | `60` | 翻译引擎空闲超时秒数 |
| `--ui` / `--no-ui` | `true` | 是否启用 Web UI |

### 环境变量

命令行参数均可通过 `MT_` 前缀环境变量设置：

```bash
export MT_HOST=0.0.0.0
export MT_PORT=8989
export MT_API_TOKEN=your_secret_token
export MT_MODEL_DIR=/opt/mtranserver/models
export MT_OFFLINE=false
export MT_LOG_LEVEL=info
export MT_MODEL_DOWNLOAD_SOURCE=mirror
export MT_MODEL_MIRROR_URL=http://183.136.206.212:8787
```

### 配置文件

路径：`~/.config/mtran/server.json`

```json
{
  "host": "0.0.0.0",
  "port": "8989",
  "apiToken": "your_secret_token",
  "modelDir": "/opt/mtranserver/models",
  "logLevel": "info",
  "enableOfflineMode": false
}
```

---

## 模型管理

### 翻译模型（自动下载）

翻译模型首次翻译某语言对时自动下载，无需手动操作。也可预下载避免首次等待：

```bash
# 预下载常用语言对（en→zh、zh→en）
node dist/main.js --download en-zh zh-en
```

模型默认存放于 `--model-dir` 指定目录，建议持久化。

### OCR 模型（手动放置）

OCR 图片翻译功能依赖 PaddleOCR 模型，**不会自动下载**，需手动放到模型目录的 `ocr/` 子目录下。

目录结构（`modelDir/ocr/`）：

```
models/ocr/
├── pp-ocrv6-tiny/              # 推荐，体积小（约 7MB）
│   ├── PP-OCRv6/
│   │   ├── det/PP-OCRv6_det_tiny.onnx
│   │   └── rec/PP-OCRv6_rec_tiny.onnx
│   └── shared/
│       └── cls/ch_PP-LCNet_x0_25_textline_ori_cls_mobile.onnx
└── pp-ocrv5-mobile/            # 备选，精度更高（约 22MB）
    ├── PP-OCRv5/
    │   ├── det/ch_PP-OCRv5_det_mobile.onnx
    │   └── rec/ch_PP-OCRv5_rec_mobile.onnx
    └── shared/
        └── cls/ch_PP-LCNet_x0_25_textline_ori_cls_mobile.onnx
```

一键下载脚本（从镜像站拉取）：

```bash
#!/bin/bash
MIRROR="http://183.136.206.212:8787"
OCR_DIR="/opt/mtranserver/models/ocr"
mkdir -p "$OCR_DIR/pp-ocrv6-tiny/PP-OCRv6/det" \
         "$OCR_DIR/pp-ocrv6-tiny/PP-OCRv6/rec" \
         "$OCR_DIR/pp-ocrv6-tiny/shared/cls"

wget -c "$MIRROR/ocr/PP-OCRv6/det/PP-OCRv6_det_tiny.onnx" -O "$OCR_DIR/pp-ocrv6-tiny/PP-OCRv6/det/PP-OCRv6_det_tiny.onnx"
wget -c "$MIRROR/ocr/PP-OCRv6/rec/PP-OCRv6_rec_tiny.onnx" -O "$OCR_DIR/pp-ocrv6-tiny/PP-OCRv6/rec/PP-OCRv6_rec_tiny.onnx"
wget -c "$MIRROR/ocr/shared/cls/ch_PP-LCNet_x0_25_textline_ori_cls_mobile.onnx" -O "$OCR_DIR/pp-ocrv6-tiny/shared/cls/ch_PP-LCNet_x0_25_textline_ori_cls_mobile.onnx"

echo "OCR 模型下载完成"
```

服务启动时会按 `pp-ocrv6-tiny` → `pp-ocrv5-mobile` 顺序查找本地模型，找到哪个用哪个。

---

## systemd 持久化

创建系统服务，实现开机自启和崩溃自动重启。

```bash
sudo tee /etc/systemd/system/mtranserver.service > /dev/null << 'EOF'
[Unit]
Description=MTranServer Translation Server
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
User=www-data
WorkingDirectory=/opt/mtranserver
ExecStart=/usr/bin/node /opt/mtranserver/dist/main.js \
  --host 0.0.0.0 \
  --port 8989 \
  --model-dir /opt/mtranserver/models \
  --log-level info
Restart=on-failure
RestartSec=10
Environment=MT_OFFLINE=false
Environment=MT_MODEL_DOWNLOAD_SOURCE=mirror
Environment=MT_MODEL_MIRROR_URL=http://183.136.206.212:8787
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
EOF
```

```bash
# 创建模型目录并授权
sudo mkdir -p /opt/mtranserver/models
sudo chown -R www-data:www-data /opt/mtranserver

# 启用服务
sudo systemctl daemon-reload
sudo systemctl enable --now mtranserver

# 查看状态和日志
sudo systemctl status mtranserver
sudo journalctl -u mtranserver -f
```

---

## Nginx 反向代理（可选）

需要 HTTPS 或自定义域名时配置：

```nginx
server {
    listen 80;
    server_name translate.example.com;

    # 翻译 API + UI
    location / {
        proxy_pass http://127.0.0.1:8989;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;

        # OCR 图片上传可能较大
        client_max_body_size 20m;
        proxy_read_timeout 120s;
    }
}
```

配合 Let's Encrypt 启用 HTTPS：

```bash
sudo apt install certbot python3-certbot-nginx
sudo certbot --nginx -d translate.example.com
```

---

## 验证部署

```bash
# 1. 健康检查
curl http://127.0.0.1:8989/

# 2. 翻译测试（en -> zh）
curl -s -X POST http://127.0.0.1:8989/translate \
  -H "Content-Type: application/json" \
  -d '{"text":"Hello world","from":"en","to":"zh-Hans"}'

# 3. OCR 识别测试（需已放置 OCR 模型）
curl -s -X POST http://127.0.0.1:8989/ocr/recognize \
  -F "image=@test.png" -F "from=en" -F "to=zh-Hans"

# 4. 图片翻译测试（返回 JSON 含 base64 图片）
curl -s -X POST "http://127.0.0.1:8989/ocr/translate-image?format=json" \
  -F "image=@test.png" -F "from=en" -F "to=zh-Hans" -F "format=json"

# 5. 浏览器访问
# Web UI:    http://<服务器IP>:8989/ui/
# API 文档:  http://<服务器IP>:8989/docs/
```

如设置了 `--api-token`，请求需加头：`-H "Authorization: Bearer your_token"`

---

## 升级与维护

### 源码构建方式升级

```bash
cd /opt/mtranserver
git pull
bun install
cd ui && bun install && bun run build && cd ..
bun run scripts/gen-ui-assets.ts
bun run scripts/gen-swagger-assets.ts
bun tsoa spec-and-routes
bun run build:node

sudo systemctl restart mtranserver
```

### npm 方式升级

```bash
npm i -g mtranserver@latest
sudo systemctl restart mtranserver
```

### 模型目录迁移

模型目录可独立挂载，升级程序不影响模型数据。迁移时改 `--model-dir` 参数或 `MT_MODEL_DIR` 环境变量即可，无需移动文件。

---

## 常见问题

**Q: 启动报 `Cannot find module` 错误**

源码构建方式需确认 `dist/main.js` 存在。若构建失败，检查 `bun run build:node` 是否成功执行。npm 方式确认全局安装路径在 `PATH` 中。

**Q: 首次翻译很慢**

首次翻译某语言对需下载模型（几十 MB），下载完成后后续为毫秒级响应。生产环境建议提前用 `--download` 预下载。

**Q: OCR 接口返回 500 或 "Failed to initialize OCR service"**

OCR 模型未正确放置。检查 `modelDir/ocr/` 目录结构是否完整，`.onnx` 文件是否下载完整（对比文件大小）。

**Q: 内存占用过高**

每个语言对的翻译引擎约占 200-400MB。减少同时加载的语言对数量，或调低 `--worker-idle-timeout`（默认 60 秒空闲后自动释放）。

**Q: 国内服务器下载模型慢**

使用镜像源（默认已启用）：`--model-source mirror`。如自建镜像站，用 `--model-mirror-url` 指定。也可配置 `--download-proxy` 走代理。

**Q: 如何限制访问**

设置 `--api-token your_secret`，所有接口需携带 `Authorization: Bearer your_secret` 头。配合 Nginx 可进一步做 IP 白名单。
