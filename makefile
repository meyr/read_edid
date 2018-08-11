.PHONY: read_edid
read_edid: main.o
	gcc $^ -o $@ -li2c

.PHONY: clean
clean:
	rm -rf *.o read_edid

ubuntu-build: Dockerfile
	docker build --no-cache -t $@:18.04 .

build-by-docker:
	docker run -it -v $(shell pwd):/home/src -w /home/src ubuntu-build:18.04 make
