CC = gcc

CFLAGS = -Wall -Wextra -std=c11 -D_POSIX_C_SOURCE=200809L -Iinclude

TARGET = bin/watchdog
WORKER = bin/worker

SRC = src/main.c \
      src/process_manager.c \
      src/signal_manager.c \
      src/logger.c


all: $(TARGET) $(WORKER)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)

$(WORKER): workers/worker.c
	$(CC) $(CFLAGS) workers/worker.c -o $(WORKER)

clean:
	rm -f $(TARGET) $(WORKER)

.PHONY: all clean
