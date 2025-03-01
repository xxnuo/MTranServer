build:
	docker build -t mtranserver .

run:
	docker run --name mtranserver -it --rm -p 8989:8989 mtranserver