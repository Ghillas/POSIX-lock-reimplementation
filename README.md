# POSIX-lock-reimplementation
This project is a reimplementation of the POSIX lock (fcntl)

The problem with the POSIX lock is when you have a lock on a file and you have a second descriptor to this file, if you close this second file descriptor, all the lock on this file are lost.

This project is a solution to this problem by using the projection of a shared memory object.
