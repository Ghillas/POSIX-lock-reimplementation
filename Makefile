UNAME := $(shell uname)
CC = cc
FLAGS = -Wall
# LIBS = ""

ifneq ($(UNAME), Darwin)
	LIBS := -lrt
endif

rl_lock_library.o: rl_lock_library.c
	$(CC) $(FLAGS) -c -o $@ $^ $(LIBS)

tests:
	# cd test/1r1w_blocking && sh run.sh
	cd test/1r1w && sh run.sh
	cd test/2writerslock && sh run.sh
	cd test/dup && sh run.sh
	cd test/dup_fork_lock && sh run.sh
	cd test/extend_lock && sh run.sh
	cd test/fusion_lock && sh run.sh
	cd test/open_close && sh run.sh
	cd test/pose_lock && sh run.sh
	cd test/split_lock && sh run.sh

clean:
	find . -type f -name '*.o' -delete
	find . -type f -name 'writer' -delete
	find . -type f -name 'main' -delete
	find . -type f -name 'main2' -delete
	find . -type f -name 'reader' -delete
	find . -type f -name 'output' -delete
	find . -type f -name 'tmp' -delete
