VERSION=1.1.0

build:
	cp ../MTranServerCore/dist/core ./core
	docker build -t xxnuo/mtranserver:$(VERSION) .
	docker tag xxnuo/mtranserver:$(VERSION) xxnuo/mtranserver:latest

export:
	docker save -o mtranserver.image.tar xxnuo/mtranserver:latest

import:
	docker load -i mtranserver.image.tar

push: build export
	docker push xxnuo/mtranserver:$(VERSION)
	docker push xxnuo/mtranserver:latest

test:
	cd example/mtranserver && docker compose down && docker compose up

run:
	cd example/mtranserver && docker compose down && docker compose up -d

.PHONY: build run export import push test
