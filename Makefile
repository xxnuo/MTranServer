.PHONY: env prepare build test

env:
	if pnpm -v > /dev/null 2>&1; then \
		echo "pnpm is already installed"; \
	else \
		echo "pnpm is not installed, please install pnpm first!"; \
		exit 1; \
	fi
	git pull
	$(MAKE) prepare

prepare:
	mkdir -p packages
	curl -L https://github.com/xxnuo/MTranServer/releases/download/core/mtran-core.tgz -o packages/mtran-core.tgz
	
build:
	docker build -t xxnuo/mtranserver:latest .

test:
	docker run -it --rm --name mtranserver-test -p 8989:8989 xxnuo/mtranserver:latest