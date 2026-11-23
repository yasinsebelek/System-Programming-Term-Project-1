CC      = gcc
CFLAGS  = -Wall -Wextra -std=c11 -D_XOPEN_SOURCE=700 -Iinclude

TARGET  = watchdog
WORKER  = bin/worker

SRC     = src/main.c \
          src/process_manager.c \
          src/signal_manager.c \
          src/logger.c
OBJ     = $(SRC:.c=.o)
HEADERS = include/process_manager.h \
          include/signal_manager.h \
          include/logger.h

all: $(TARGET) $(WORKER)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) -o $(TARGET)

src/%.o: src/%.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

bin:
	mkdir -p bin

$(WORKER): workers/worker.c | bin
	$(CC) $(CFLAGS) $< -o $@

test: all
	./tests/test_scenarios.sh

clean:
	rm -f $(TARGET) $(WORKER) $(OBJ)
	rmdir bin 2>/dev/null || true

.PHONY: all clean test
