.PHONY: download download-core download-records generate-docs

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
	@GOOS= GOARCH= go generate ./bin
	@echo "Downloaded core binary from repository successfully"

download-records:
	touch ./data/records.json
	@GOOS= GOARCH= go generate ./data

download: download-core download-records
	@echo "Downloaded successfully"

generate-docs:
	@echo "Generating docs..."
	@go run github.com/swaggo/swag/cmd/swag@latest init -g ./cmd/mtranserver/main.go -o ./internal/docs
	@echo "Docs generated successfully"

build: generate-docs
	@echo "Building..."
	@go build -o ./dist/mtranserver-$(GOOS)-$(GOARCH)$(SUFFIX) ./cmd/mtranserver
	@echo "Built successfully"
