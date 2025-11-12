TARGETS= warden
PREFIX ?= /usr/local

warden:
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
	rm -fr warden
