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
	@echo "Detecting platform: $(GOOS)-$(GOARCH)"
	@echo "Downloading $(WORKER_BINARY) from $(DOWNLOAD_URL)..."
	@mkdir -p bin
	@rm -f bin/worker$(SUFFIX)
	@curl -L -o bin/worker$(SUFFIX) $(DOWNLOAD_URL) || (echo "Failed to download worker binary" && exit 1)
	@chmod +x bin/worker$(SUFFIX)
	@echo "Downloaded successfully to bin/worker$(SUFFIX)"

build-core:
	@echo "Building core..."
	@mkdir -p bin
	@rm -f bin/worker$(SUFFIX)
	@cd ../../MTranCore && make build-worker
	@cp ../../MTranCore/build/worker bin/worker$(SUFFIX)
	@chmod +x bin/worker$(SUFFIX)
	@echo "Built successfully to bin/worker$(SUFFIX)"