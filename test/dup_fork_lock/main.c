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
    struct flock lock = {.l_start = 0, .l_len = 2, .l_type = F_RDLCK, .l_whence = SEEK_SET};
    int set = rl_fcntl(rl_fd, 0, &lock);
    status = status && (nb_locks_on_file(rl_fd) == 1);
    if(set == -1) {
        printf("rl_fcntl n'a pas fonctionner\n");
    } else {
        printf("rl_fcntl a fonctionner\n");
    }
    print_rl_all_files();
    printf("rl_dup\n");
    rl_descriptor dup_desc = rl_dup(rl_fd);
    print_rl_all_files();

     puts("rl fork");
    pid_t fd = rl_fork();

    
    if (fd < 0) {
        printf("rl_fork fail\n");
        exit(1);
    } else if ( fd == 0 ) {
        printf("je suis %d \n",getpid());
        print_rl_all_files();
        sleep(2);
        printf("START le fils\n");
        printf("closes\n");
        printf("je suis %d \n",getpid());
        print_rl_all_files();
        rl_close(rl_fd);
        rl_close(dup_desc);
        print_rl_all_files();
        printf("j'ai fait les closes\n");
        printf("END le fils\n");
        return 0;
    } else {
        sleep(1);
        print_rl_all_files();
        printf("START le pere\n");
	lock.l_type = F_UNLCK;
	rl_fcntl(rl_fd, 0, &lock);
	print_rl_all_files();
        printf("closes\n");
        rl_close(rl_fd);
        rl_close(dup_desc);
        print_rl_all_files();
        printf("j'ai fait les closes\n");
        printf("END le pere\n");
    }
    
    return 0;
}


    
   
