NAME  := book-dev
IMAGE := dev
DIR   := $(CURDIR)

.PHONY: up shell down build

up:
	docker run -d --name $(NAME) \
		-v $(DIR):$(DIR) \
		-w $(DIR) \
		$(IMAGE) sleep infinity

shell:
	docker exec -it $(NAME) bash

build:
	docker exec $(NAME) bash -c 'cmake -B build -G Ninja && cmake --build build'

down:
	docker rm -f $(NAME)
