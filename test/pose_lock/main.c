#include "../../rl_lock_library.h"
#include <stdbool.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

#define file_path "files/test.txt"

int main() {
    rl_init_library();
    bool status = true;
    rl_descriptor rl_fd  = rl_open(file_path, O_RDWR, 0755);
    if (rl_fd.d == -1){
        fprintf(stderr, "open fails\n");
        exit(1);
    }
    struct flock lock = {.l_start = 0, .l_len = 2, .l_type = F_WRLCK, .l_whence = SEEK_SET};
    rl_fcntl(rl_fd, 0, &lock);
    status = status && (nb_locks_on_file(rl_fd) == 1);
    lock.l_type = F_UNLCK;
    rl_fcntl(rl_fd, 0, &lock);
    status = status && (nb_locks_on_file(rl_fd) == 0);
    rl_close(rl_fd);
    return status ? 0 : 1;
}