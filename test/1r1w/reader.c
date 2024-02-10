#include "../../rl_lock_library.h"
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

#define file_path "files/test.txt"
#define who "lecteur\n"

int main() {
    puts(who);
    rl_init_library();
    rl_descriptor rl_fd  = rl_open(file_path, O_RDWR, 0755);
    if (rl_fd.d == -1){
        fprintf(stderr, "open fails\n");
        exit(1);
    }
    printf("on pose un verrou en lecture des octets 0 a 2\n");
    struct flock lock = {.l_start = 0, .l_len = 2, .l_type = F_RDLCK, .l_whence = SEEK_SET};
    int set;
    set = rl_fcntl(rl_fd, F_SETLKW, &lock);
    //printf("je suis dans la boucle\n");
    
    if(set < 0) {
      printf("le verrou n'a pas été posé\n");
    } else {
      printf("le verrou a été posé\n");
    }
    sleep(15);
    lock.l_type = F_UNLCK;
    
    rl_close(rl_fd);
    
    return set;
}
