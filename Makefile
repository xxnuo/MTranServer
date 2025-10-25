.PHONY: download-core

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
	@go generate ./bin
	@echo "Downloaded core binary from repository successfully"

build-core:
	@echo "Building core..."
	@rm -f bin/worker
	@cd ../../MTranCore && make build-worker
	@cp ../../MTranCore/build/worker bin/worker
	@chmod +x bin/worker
	@echo "Built successfully to bin/worker"
	@go generate ./bin/gen_hash.go
	@echo "Generated successfully to bin/bin.go"

download-records:
	@go run ./data/gen_records.go
