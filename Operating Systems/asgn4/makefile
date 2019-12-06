all: 
	cc aofs.c -o aofs `pkgconf fuse --cflags --libs`

init:
	cc init_FS_FILE.c -o init

benchmark:
	cc benchmark.c -o benchmark

clean:
	rm aofs benchmark init
