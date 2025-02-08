.PHONY: build-dev run-dev run-mts run-mt dev-build dev-run build dev

run-mts:
	nohup bin/mts > mts.log 2>&1 &

run-mt:
	nohup mt > mt.log 2>&1 &

dev-build:
	docker build -t mtranserver-dev -f dev.Dockerfile .

dev-run:
	docker run -it --rm --name mtranserver-dev -v $(PWD):/app -w /app mtranserver-dev /bin/bash

build:
	go build -o mt main.go

dev:
	go run main.go