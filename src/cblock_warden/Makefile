TARGETS= cblock_warden
PREFIX ?= /usr/local

cblock_warden:
	go mod tidy
	go build

install:
	[ -d $(PREFIX)/bin ] || mkdir -p $(PREFIX)/bin
	install -m 0555 $(TARGETS) $(PREFIX)/bin

lint:
	$(HOME)/go/bin/golangci-lint run

test:
	go test -v

clean:
	rm -fr cblock_warden
