CC=gcc
# 并行版使用 POSIX 线程（pthread）。
CFLAGS=-std=gnu11 -g -O3 -pthread -Wall -Werror -pedantic
# 串行参考版保持原有编译选项，用于结果对比。
CFLAGS_SERIAL=-std=gnu11 -g -O3 -pthread -Wall -Werror -pedantic
# ASAN = Address SANitizer
CFLAGS_ASAN=-std=gnu11 -g -Og -pthread -fsanitize=address -Wall -Werror -pedantic
# TSAN = Thread SANitizer
CFLAGS_TSAN=-std=gnu11 -g -Og -pthread -fsanitize=thread -Wall -Werror -pedantic

all: life life-serial life-asan

%-asan.o: %.c
	$(CC) -c $(CFLAGS_ASAN) -o $@ $<
%-tsan.o: %.c
	$(CC) -c $(CFLAGS_TSAN) -o $@ $<

life.o: life.c life.h
life-asan.o: life.c life.h
life-serial.o: life-serial.c life.h
life-serial-asan.o: life-serial.c life.h
life-parallel.o: life-parallel.c life.h
life-parallel-asan.o: life-parallel.c life.h
main.o: main.c life.h
main-asan.o: main.c life.h
main-parallel.o: main-parallel.c life.h
main-parallel-asan.o: main-parallel.c life.h

# 提交评分用的目标：多线程并行版入口。
life: main-parallel.o life.o life-parallel.o
	$(CC) $(CFLAGS) -o $@ $^ -lpthread

# 串行参考版（与主办方框架行为一致），仅用于本地对比验证。
life-serial: main.o life.o life-serial.o
	$(CC) $(CFLAGS_SERIAL) -o $@ $^

life-asan: main-parallel-asan.o life-asan.o life-parallel-asan.o
	$(CC) $(CFLAGS_ASAN) -o $@ $^

clean:
	rm -f *.o life life-serial life-asan

SUBMIT_FILENAME=life-submission-$(shell date +%Y%m%d%H%M%S).tar.gz

archive:
	tar -zcf $(SUBMIT_FILENAME) $(wildcard *.c *.h *.txt *.md *.pdf) Makefile
	@echo "Created $(SUBMIT_FILENAME); please upload and submit this file."

submit: archive

.PHONY: all clean archive submit
