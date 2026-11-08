CFLAGS=-O2 -std=c99 -Ithird_party -ffp-contract=off -Wall -Wno-unused-function -Wno-misleading-indentation -Wno-unused-result
SRC=src/grus.c src/grus_data.c
all: compare_d compare_f bench render
src/grus_data.c: data/export.json tools/gen_data.py
	python3 tools/gen_data.py
compare_d: $(SRC) test/compare.c src/grus.h
	$(CC) $(CFLAGS) -DGRUS_DOUBLE -o $@ $(SRC) test/compare.c -lm
compare_f: $(SRC) test/compare.c src/grus.h
	$(CC) $(CFLAGS) -o $@ $(SRC) test/compare.c -lm
clean:
	rm -f compare_d compare_f bench render
bench: $(SRC) test/bench.c src/grus.h
	$(CC) $(CFLAGS) -D_POSIX_C_SOURCE=199309L -o $@ $(SRC) test/bench.c -lm
render: $(SRC) test/render.c src/grus.h
	$(CC) $(CFLAGS) -o $@ $(SRC) test/render.c -lm
