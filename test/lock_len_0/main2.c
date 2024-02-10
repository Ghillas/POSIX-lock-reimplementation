#include "../../rl_lock_library.h"
#include <stdbool.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

#define file_path "files/test.txt"

int main() {
    bool status = true;
    rl_descriptor rl_fd  = rl_open(file_path, O_RDWR, 0755);
    if (rl_fd.d == -1){
        fprintf(stderr, "open fails\n");
        exit(1);
    }
    struct flock lock = {.l_start = 3, .l_len = 2, .l_type = F_WRLCK, .l_whence = SEEK_SET};
    int res = rl_fcntl(rl_fd, 0, &lock);
    status = status && (nb_locks_on_file(rl_fd) == 1);
    
    if(res < 0) {
    	printf("le verrou n'a pas été posé\n");
    } else {
    	printf("le verrou a été posé\n");
    }	
    
    rl_close(rl_fd);
    return status ? 0 : 1;
}
