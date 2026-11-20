all:
	gcc -Wall -Wextra -Iinclude src/main.c src/process_manager.c -o bin/watchdog
	gcc -Wall -Wextra workers/worker.c -o bin/worker

clean:
	rm -f bin/watchdog bin/worker
