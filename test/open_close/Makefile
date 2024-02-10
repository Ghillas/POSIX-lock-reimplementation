UNAME := $(shell uname)
CC = "cc"
FLAGS = "-Wall"
LIBS = ""

ifneq ($(UNAME), Darwin)
	LIBS := "-lrt"
endif

main: main.c
	$(CC) $(FLAGS) -o $@ ../../rl_lock_library.c $< $(LIBS) 