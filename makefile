all: cache-sim

cache-sim: clean
	gcc cache-sim.c -std=c99 -lm -o cache-sim
	
clean:
	rm cache-sim -f
	
debug:	
	gcc cache-sim.c -g -std=c99 -lm -o cache-sim