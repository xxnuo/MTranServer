build:
	docker build -t xxnuo/mtranserver:1.0.0 .
	docker tag xxnuo/mtranserver:1.0.0 xxnuo/mtranserver:latest

run:
	docker run --name mtranserver -it --rm -p 8989:8989 xxnuo/mtranserver:1.0.0

export:
	docker save -o mtranserver.image.tar xxnuo/mtranserver:latest

import:
	docker load -i mtranserver.image.tar

push:
	docker push xxnuo/mtranserver:1.0.0
	docker push xxnuo/mtranserver:latest

compose:
	docker compose up

.PHONY: build run export import push compose