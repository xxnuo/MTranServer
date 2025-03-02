.PHONY: build-dev run-dev run-core run-mt dev-build dev-run build dev

dev-build:
	docker build -t mtranserver-dev -f dev.Dockerfile .

dev-run:
	docker run -it --rm --name mtranserver-dev -p 8990:8990 -p 8989:8989 -v $(PWD):/app -w /app mtranserver-dev /bin/bash

build:
	go build -o mt main.go

dev:
	go run main.go