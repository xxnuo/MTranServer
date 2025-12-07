.PHONY: build-ui download download-core download-records generate-docs

# Detect OS and architecture
GOOS ?= $(shell go env GOOS)
GOARCH ?= $(shell go env GOARCH)

# Set binary suffix for Windows, js
ifeq ($(GOOS),windows)
	SUFFIX := .exe
else ifeq ($(GOOS),js)
	SUFFIX := .wasm
else
	SUFFIX :=
endif

# GitHub release URL
GITHUB_REPO := xxnuo/MTranCore
RELEASE_TAG := latest
WORKER_BINARY := worker-$(GOOS)-$(GOARCH)$(SUFFIX)
DOWNLOAD_URL := https://github.com/$(GITHUB_REPO)/releases/latest/download/$(WORKER_BINARY)

# Download core binary from https://github.com/xxnuo/MTranCore/releases/latest
# Support: linux-amd64, linux-arm64, linux-386, windows-amd64, darwin-amd64, darwin-arm64
# Extra: js-wasm
download-core:
	touch ./bin/worker
	@TARGET_GOOS=$(GOOS) TARGET_GOARCH=$(GOARCH) GOOS= GOARCH= go generate ./bin
	@echo "Downloaded core binary from repository successfully"

download-records:
	touch ./data/records.json
	@GOOS= GOARCH= go generate ./data

download: download-core download-records
	@echo "Downloaded successfully"

generate-docs:
	@echo "Generating docs..."
	@GOOS= GOARCH= go run github.com/swaggo/swag/cmd/swag@latest init -g ./cmd/mtranserver/main.go -o ./internal/docs
	@echo "Docs generated successfully"

build-ui:
	@echo "Building UI..."
	@cd ui && pnpm install && pnpm build
	@echo "UI built successfully"

# Get version from git tag
VERSION ?= $(shell git describe --tags --always --dirty 2>/dev/null || echo "v0.0.0-dev")
LDFLAGS := -X github.com/xxnuo/MTranServer/internal/version.Version=$(VERSION)

build: generate-docs
	@echo "Building version $(VERSION)..."
	@go build -ldflags "$(LDFLAGS)" -o ./dist/mtranserver-$(GOOS)-$(GOARCH)$(SUFFIX) ./cmd/mtranserver
	@echo "Built successfully"

dev: generate-docs
	@echo "Building version $(VERSION)..."
	MT_LOG_LEVEL=debug MT_ENABLE_UI=true go run -ldflags "$(LDFLAGS)" ./cmd/mtranserver/main.go
