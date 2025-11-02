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
	$(MAKE) update

update:
	mkdir -p packages
	LATEST_VERSION=$$(curl -s https://api.github.com/repos/xxnuo/MTranCore/releases/latest | grep -o '"tag_name": "v[^"]*"' | cut -d'"' -f4); \
	echo "Downloading latest version: $$LATEST_VERSION"; \
	curl -L "https://github.com/xxnuo/MTranCore/releases/download/$$LATEST_VERSION/mtran-core-$${LATEST_VERSION#v}.tgz" -o packages/mtran-core.tgz

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
	pnpm i
	LOG_LEVEL=debug node --expose-gc js/mts.js

watch:
	pnpm i
	LOG_LEVEL=debug nodemon --watch js --ext js --exec "node --expose-gc js/mts.js"

trace:
	pnpm i
	LOG_LEVEL=debug node --expose-gc --inspect-brk js/mts.js