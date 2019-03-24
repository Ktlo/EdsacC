export CC=${PREFIX}g++${POSTFIX} -std=c++17 ${DEBUG}

build:
	echo ${CC}
	make -C src
	cp src/edsacc${EXT} .

win32:
	CC="x86_64-w64-mingw32-g++ -mconsole -std=c++17" EXT=".exe" make -C src

clean:
	make -C src clean
