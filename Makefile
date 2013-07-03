all: MAIN.o DECODER.o
	gcc MAIN.o DECODER.o -o gif2sopt

clean:
	rm MAIN.O DECODER.o gif2sopt

MAIN.o: MAIN.C
	gcc -c MAIN.C

DECODER.o: DECODER.C
	gcc -c DECODER.C
