all:
	gcc src/main.c -o bin/watchdog
	gcc workers/worker.c -o bin/worker

clean:
	rm -f bin/watchdog bin/worker
