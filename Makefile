CC = gcc

CFLAGS = -Wall -Wextra -std=c11 -D_XOPEN_SOURCE=700 -Iinclude

TARGET = watchdog
WORKER = bin/worker

SRC = src/main.c \
      src/process_manager.c \
      src/signal_manager.c \
      src/logger.c


all: $(TARGET) $(WORKER)

bin:
	mkdir -p bin

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)

$(WORKER): workers/worker.c | bin
	$(CC) $(CFLAGS) workers/worker.c -o $(WORKER)

clean:
	rm -f $(TARGET) $(WORKER)
	rmdir bin 2>/dev/null || true

.PHONY: all clean
