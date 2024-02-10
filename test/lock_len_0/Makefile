UNAME := $(shell uname)
CC = "cc"
FLAGS = "-Wall"
LIBS = ""


ifneq ($(UNAME), Darwin)
	LIBS := "-lrt"
endif

.PHONY: all

all:
	make main && make main2

%: %.c
	$(CC) $(FLAGS) -o $@ ../../rl_lock_library.c $< $(LIBS) 

clean:
	rm main main2
