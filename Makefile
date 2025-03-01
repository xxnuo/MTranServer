build:
	docker build -t mtranserver:1.0.0 .

run:
	docker run --name mtranserver -it --rm -p 8989:8989 mtranserver

export:
	docker save -o mtranserver.image.tar mtranserver:1.0.0

import:
	docker load -i mtranserver.image.tar
	docker tag mtranserver:1.0.0 xxnuo/mtranserver:1.0.0

.PHONY: build run export import
