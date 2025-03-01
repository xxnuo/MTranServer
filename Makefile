build:
	docker build -t mtranserver .

run:
	docker run --name mtranserver -it --rm -p 8989:8989 mtranserver

export:
	docker save -o mtranserver.image.tar mtranserver

import:
	docker load -i mtranserver.image.tar

.PHONY: build run export import
