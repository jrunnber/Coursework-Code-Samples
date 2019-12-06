all: stress benchmark 

stress:
	cc -o stress -D_GNU_SOURCE stress.c
benchmark:
	cc -o benchmark benchmark.c
clean:
	rm stress benchmark
