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
	@rm -f bin/worker
	@curl -L -o bin/worker$(SUFFIX) $(DOWNLOAD_URL) || (echo "Failed to download worker binary" && exit 1)
	@chmod +x bin/worker
	@echo "Downloaded successfully to bin/worker"
	@go generate ./bin
	@echo "Generated successfully to bin/bin.go"

build-core:
	@echo "Building core..."
	@mkdir -p bin
	@rm -f bin/worker
	@cd ../../MTranCore && make build-worker
	@cp ../../MTranCore/build/worker bin/worker
	@chmod +x bin/worker
	@echo "Built successfully to bin/worker"
	@go generate ./bin
	@echo "Generated successfully to bin/bin.go"

download-records:
	@mkdir -p data
	@curl -L -o data/records.json https://remote-settings.mozilla.org/v1/buckets/main/collections/translations-models/records
	@echo "Downloaded successfully to data/records.json"