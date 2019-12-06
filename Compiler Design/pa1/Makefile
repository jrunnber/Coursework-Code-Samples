#Commnet
CC = g++
CFLAGS = -g -O0 -Wall -Wextra -std=gnu++14 
OBJECTS = main.o string_set.o 
HEADERS = string_set.h 

.PHONY: all
all: main

 main: $(OBJECTS)
	$(CC) $(CFLAGS) $(OBJECTS) -o main -lm
 main.o: main.cpp $(HEADERS)
	$(CC) $(CFLAGS) -c main.cpp

string_set.o: string_set.cpp string_set.h
	$(CC) $(CFLAGS) -c string_set.cpp

.PHONY: clean
clean:
	rm -f $(OBJECTS) main
