all: lsperf.c
	gcc -Wall -O2 -o lsperf lsperf.c -pthread -lrt -laio

debug: lsperf.c
	gcc -Wall -g -DDEBUGLSPERF -o lsperf lsperf.c -pthread -lrt -laio

install: lsperf lsperf.1
	cp lsperf.1 /usr/local/man/man1/
	cp lsperf /usr/local/bin/
clean:
	@ rm -f lsperf

VERSION=`grep "\#define LSPERFVERSION" lsperf.c| cut -d \" -f 2`
LSPERFDIR=lsperf-$(VERSION)
tgz: lsperf.1 lsperf.c Makefile
	@mkdir -p lsperf-$(VERSION)
	@cp  Makefile lsperf.c lsperf.1 DEBUG README $(LSPERFDIR)
	@tar czf $(LSPERFDIR).tar.gz $(LSPERFDIR)
	@ls $(LSPERFDIR).tar.gz
