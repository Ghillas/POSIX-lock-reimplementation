#ifndef RL_LOCK_LIBRARY
#define RL_LOCK_LIBRARY

#include <stddef.h>
#include <unistd.h>
#include <fcntl.h>
#include <semaphore.h>

typedef int fd_t;

typedef struct {
  pid_t proc; // pid du processus
  fd_t des; // descripteur du fichier
} owner;


#define NB_OWNERS 10 /* 10 pour test mais a changer si une autre valeur est mieux*/

typedef struct {
  int next_lock;
  off_t starting_offset; // decalage du fichier du segment par rapport au debut du fichier
  off_t len; // longueur du segment (si len==0 le segment va jusqu'a la fin du fichier)
  short type; // type du verrou (identique aux constantes du champ l_type pour flock)
  size_t nb_owners; // nombre de propriétaire effectif du verrou
  owner lock_owners[NB_OWNERS]; // tableau des propriétaire du verrou
} rl_lock;


typedef struct {
  sem_t writer;
  rl_lock lock;
} rl_sem_lock;


#define NB_LOCKS 10 // je ne sais pas quelle valeur il faut mettre
#define NB_OPENER 100 // NB_LOCK * NB_OWNER

typedef struct {
  int first;
  rl_sem_lock lock_table[NB_LOCKS];
  int descripteur_ouvert;
  owner opener[NB_OPENER]; // tableau des processus qui ont fait open sur le fichier
  sem_t semaphore;
} rl_open_file;


typedef struct {
  fd_t d;
  rl_open_file *f;
  const char * shm_name;
} rl_descriptor;


rl_descriptor rl_open(const char *path, int oflag, ...);

int rl_close(rl_descriptor lfd);

int rl_fcntl(rl_descriptor lfd, int cmd, struct flock *lck);

rl_descriptor rl_dup(rl_descriptor lfd) ;

rl_descriptor rl_dup2(rl_descriptor lfd, int newd);

size_t nb_locks_on_file(rl_descriptor lfd);

// int tmp_fcntl(rl_descriptor lfd, int cmd, struct flock* lock);

pid_t rl_fork();

int rl_init_library();

void print_rl_all_files();

#endif
