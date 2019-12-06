all: 
	cc aofs.c -o aofs `pkgconf fuse --cflags --libs`

benchmark:
	cc benchmark.c -o benchmark

clean:
	rm aofs benchmark
