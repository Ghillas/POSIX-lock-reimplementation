UNAME := $(shell uname)
CC = "cc"
FLAGS = "-Wall"
LIBS = ""

ifneq ($(UNAME), Darwin)
	LIBS := "-lrt"
endif

.PHONY: all

all:
	make writer && make reader

%: %.c
	$(CC) $(FLAGS) -o $@ ../../rl_lock_library.c $< $(LIBS) 

clean:
	rm writer reader