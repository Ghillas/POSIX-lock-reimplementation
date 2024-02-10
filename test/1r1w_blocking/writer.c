#include "../../rl_lock_library.h"
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define file_path "files/test.txt"
#define who "ecrivain\n"

int main() {
    puts(who);
    rl_init_library();
    rl_descriptor rl_fd  = rl_open(file_path, O_RDWR, 0755);
    if (rl_fd.d == -1){
        fprintf(stderr, "open fails\n");
        exit(1);
    }
    struct flock lock = {.l_start = 0, .l_len = 2, .l_type = F_WRLCK, .l_whence = SEEK_SET};
    int set = rl_fcntl(rl_fd, F_SETLKW, &lock);
    sleep(3);
    lock.l_type = F_UNLCK;
    rl_fcntl(rl_fd, 0, &lock);
    rl_close(rl_fd);
    return set;
}