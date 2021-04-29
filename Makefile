all: generator processor
generator: generator.c	
	gcc -Wall -o generator  generator.c -lrt 

processor: processor.c	
	gcc -Wall -o processor  processor.c -lrt 

.PHONY: clean all
clean:
	rm generator  processor