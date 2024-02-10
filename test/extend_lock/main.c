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
    // struct flock lock = {.l_start = 0, .l_len = 3, .l_type = F_WRLCK, .l_whence = SEEK_SET};
    struct flock lock = {.l_start = 0, .l_len = 3, .l_type = F_WRLCK, .l_whence = SEEK_SET};
    int set = rl_fcntl(rl_fd, 0, &lock);
    status = status && (nb_locks_on_file(rl_fd) == 1);
    print_rl_all_files();
    lock.l_start = 0;
    lock.l_len = 7;
    rl_fcntl(rl_fd, 0, &lock);
    size_t nb_lock = nb_locks_on_file(rl_fd);
    // printf("NB LOCK = %lu\n", nb_lock);
    status = status && ( nb_lock == 1 );
    print_rl_all_files();
    lock.l_type = F_UNLCK;
    rl_fcntl(rl_fd, 0, &lock);
    nb_lock = nb_locks_on_file(rl_fd);
    // printf("NB LOCK = %lu\n", nb_lock);
    status = status && (nb_lock == 0);
    print_rl_all_files();
    rl_close(rl_fd);
    return status ? 0 : 1;
}
