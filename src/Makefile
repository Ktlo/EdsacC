
all: edsacc${EXT}

edsacc${EXT}: parser.o main.o arguments.o
	${CC} $^ -o $@

parser.o: parser.cpp
	${CC} -c $^

main.o: main.cpp
	${CC} -c $^

arguments.o: arguments.cpp
	${CC} -c $^

clean:
	rm -f *.o edsacc${EXT}
