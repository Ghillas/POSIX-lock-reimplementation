#include "../../rl_lock_library.h"
#include <stdbool.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

#define file_path "files/test.txt"

int main() {
    rl_init_library();
    bool status = true;
    rl_descriptor rl_fd  = rl_open(file_path, O_RDWR);
    if (rl_fd.d == -1){
        fprintf(stderr, "open fails\n");
        exit(1);
    }
    printf("rl_fcntl\n");
    struct flock lock = {.l_start = 0, .l_len = 7, .l_type = F_WRLCK, .l_whence = SEEK_SET};
    int set = rl_fcntl(rl_fd, 0, &lock);
    if(set < 0) {
      printf("le verrou n'a pas été posé\n");
      exit(1);
    } else {
      print_rl_all_files();
    }
    printf("rl_dup\n");
    status = status && (nb_locks_on_file(rl_fd) == 1);
    rl_descriptor rl_fd_2 = rl_dup(rl_fd);
    status = status && (nb_locks_on_file(rl_fd_2) == 1);
    print_rl_all_files();
    rl_close(rl_fd);
    rl_close(rl_fd_2);
    return status ? 0 : 1;
}
