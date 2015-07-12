CC=gcc
CCFLAGS=-c
LDFLAGS=-lrt -lpthread
SOURCES=proj_game.c workq.c
OBJECTS=$(SOURCES:%.c=%.o)
OUTPUTFILES=$(wildcard workqueuedump*)
EXECUTABLE=proj_game

all: $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS)
	$(CC) $(LDFLAGS) $(OBJECTS) -o $(EXECUTABLE)

%.o: %.c
	$(CC) $(CCFLAGS)  $< -o $@

clean:
	rm -rf $(OBJECTS) $(EXECUTABLE) $(OUTPUTFILES)

.PHONY: all clean
