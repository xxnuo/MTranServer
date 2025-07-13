.PHONY: env prepare build test

env:
	if pnpm -v > /dev/null 2>&1; then \
		echo "pnpm is already installed"; \
	else \
		echo "pnpm is not installed, please install pnpm first!"; \
		exit 1; \
	fi
	git pull
	pnpm i -g nodemon
	$(MAKE) prepare

prepare:
	mkdir -p packages
	curl -L https://github.com/xxnuo/MTranServer/releases/download/core/mtran-core.tgz -o packages/mtran-core.tgz
	
build:
	docker build -t xxnuo/mtranserver:test \
    -f Dockerfile .

build-zh:
	docker build -t xxnuo/mtranserver:test-zh \
    --build-arg PRELOAD_SRC_LANG=zh-Hans \
    --build-arg PRELOAD_TARGET_LANG=zh-Hans \
    -f Dockerfile.model .

test: build
	docker run -it --rm --name mtranserver-test -p 8989:8989 xxnuo/mtranserver:test

test-zh: build-zh
	docker run -it --rm --name mtranserver-test-zh -p 8989:8989 xxnuo/mtranserver:test-zh

dev:
	node js/mts.js

watch:
	nodemon --watch js --ext js --exec "node js/mts.js"