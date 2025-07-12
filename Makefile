.PHONY: env prepare-js

env:
	if pnpm -v > /dev/null 2>&1; then \
		echo "pnpm is already installed"; \
	else \
		echo "pnpm is not installed, please install pnpm first!"; \
		exit 1; \
	fi
	git pull
	$(MAKE) prepare-js

prepare-js:
	mkdir -p packages
	curl -L https://github.com/xxnuo/MTranServer/releases/download/core/mtran-core.tgz -o packages/mtran-core.tgz
	pnpm install