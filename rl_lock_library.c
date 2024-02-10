// Ghillas Immoune
// Yves Ndiaye


#include "rl_lock_library.h"
#include <semaphore.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <errno.h>
#include <string.h>
#include <stdarg.h>
#include <signal.h>
#include <limits.h>

// Nom de la variable d'environmment qui coorespond au "f" du shared object
#define RL_PREFIX_OBJ "RL_PREFIX_OBJ"

// Value par default si [RL_PREFIX_OBJ] variable d'environememnt n'est pas definie
#define RL_PREFIX_DEFAULT "f"
#define RL_SHM_FORMAT "/%s_%ld_%ld"

#define MMAPP_ERROR_VALUE ((void *) -1)
#define MALLOC_FAIL -2
#define NOT_USED_SQUARE -2
#define OWNER_NOT_FOUND -1
#define OWNER_FULL -1
#define RL_LOCK_NOT_FOUND -1
#define LOCK_FULL -1
#define OPENER_NOT_FOUND -1
#define OPENER_FULL -1
#define NB_FILES 256
#define SEM_PROC_SYNC 1

#define log(message) \
    printf("LINE : %d : %s\n", __LINE__, message)

#define min(lhs, rhs) \
    (lhs > rhs ? rhs : lhs)

#define max(lhs, rhs) \
    (lhs < rhs ? rhs : lhs)

#define eq_owner(lhs, rhs) \
    (lhs.proc == rhs.proc && lhs.des == rhs.des)

#define eq_string(lhs, rhs) \
    (strcmp(lhs, rhs) == 0)

#define added_owner(owner) \
    printf("__LINE__ = %d, Added forked owner = {.proc = %d, descri = %d}\n", __LINE__, owner.proc, owner.des)

#define is_lock_entire_file(lock) \
    ((bool) lock.l_len == 0)

static int index_lock_libre(rl_open_file *rof);
static void add_lock(rl_open_file *rof, rl_lock loc, int index_add, bool);
int tmp_fcntl(rl_descriptor lfd, int cmd, struct flock* lock);
int tmp_fcntl_aux(rl_descriptor lfd, int cmd, struct flock* lock);
void print_rl_all_files();


// Struct utilisée pour contenir la liste des index des rl_locks devauchangement 
// ou etant inclue sur une certaine region
typedef struct rl_sem_lock_array_t {
    int locks_index[NB_LOCKS];
    size_t count;
} lock_array_t;


static struct {
    int nb_files;
    rl_open_file* tab_open_files[NB_FILES];
} rl_all_files;

static int owner_index(rl_lock lock, owner o) {
    for (size_t i = 0; i < lock.nb_owners && i < NB_OWNERS; i += 1) {
        owner inner_owner =  lock.lock_owners[i];
        if ( eq_owner(o, inner_owner) ) return i;
    }
    return OWNER_NOT_FOUND;
}

static bool is_owner(rl_lock lock, owner o) {
    return owner_index(lock, o) >= 0;
}

/**
    @param rm_lock_index l'index dans la liste chainee à supprimer       
*/
static void rm_lock(rl_open_file* rof, int rm_lock_index) {
    int suiv = rof->first;
    rl_sem_lock* rsl = rof->lock_table + rm_lock_index;
    if (rof->first == NOT_USED_SQUARE) return;
    if(rm_lock_index == rof->first) { // cas ou le lock a supprimer est le 1er
        
        if(rof->lock_table[rof->first].lock.next_lock >= 0) { // cas ou il y a plus d'un lock
            int new_first = rof->lock_table[rof->first].lock.next_lock;
            rof->lock_table[rof->first].lock.next_lock = NOT_USED_SQUARE;
            rof->first = new_first;
        } else { // cas ou il y a seulement un lock
            rof->first = NOT_USED_SQUARE;
        }
        sem_post(&rsl->writer); // Libere le semaphore quand le lock est détruit
        
    } else {
        while(suiv >= 0) {
            if (rof->lock_table[suiv].lock.next_lock == rm_lock_index) {
                rl_lock next = rof->lock_table[rm_lock_index].lock;
                rof->lock_table[suiv].lock.next_lock = next.next_lock;
                rof->lock_table[rm_lock_index].lock.next_lock = NOT_USED_SQUARE;
                sem_post(&rsl->writer);
                break;
            }
            suiv = rof->lock_table[suiv].lock.next_lock;
        }
    }
    return;
}

static bool has_one_owner(rl_lock lock) {
    return lock.nb_owners == 1;
}

static bool is_only_owner(rl_lock lock, owner o) {
    return has_one_owner(lock) && is_owner(lock, o);
}

static void delete_owner_index(rl_lock* lock, int index, int array_len) {
    // Supprimer a l'index [index] + potentiellement deplacé tt les element suivant
    for (int i = index; i < array_len - 1; i++) {
        lock->lock_owners[index] = lock->lock_owners[index + 1];
    }
}

static bool is_left_interval_overlap(const off_t base_start, const off_t base_len, const off_t n_start, const off_t n_length) {
    const off_t base_end = base_start + base_len;
    // const off_t n_end = n_start + n_length;
    return n_start >= base_start && n_start <= base_end ;
}

static bool is_right_interval_overlap(const off_t base_start, const off_t base_len, const off_t n_start, const off_t n_length) {
    return is_left_interval_overlap(n_start, n_length, base_start, base_len);
}

static bool is_interval_overlap(const off_t base_start, const off_t base_len, const off_t n_start, const off_t n_length) {
    off_t b_len = base_len;
    if(b_len == 0) { // si un lock a la len qui vaut 0 on lui donne la valeur MAX_INT pour considerer tout le fichier
        b_len = INT_MAX;
    }
    return is_right_interval_overlap(base_start, b_len, n_start, n_length)
    || is_left_interval_overlap(base_start, b_len, n_start, n_length) ;
}

/**
    Retourne si base_start <= n_start <= (n_start + n_length) < base_start + base_len

*/
static bool is_interval_included(const off_t base_start, const off_t base_len, const off_t n_start, const off_t n_length) {
    off_t b_len = base_len;
    if(b_len == 0) { // si un lock a la len qui vaut 0 on lui donne la valeur MAX_INT pour considerer tout le fichier
        b_len = INT_MAX;
    }
    const off_t base_end = base_start + b_len;
    const off_t n_end = n_start + n_length;
    return n_start >= base_start && n_start <= base_end && n_end < base_end;
}

static bool has_set_conflicts_lock(rl_descriptor rd, owner lowner, off_t start, off_t length, bool is_reader) {
    rl_open_file* rof = rd.f;
    int suiv = rof->first;
    while(suiv >= 0) {
        rl_lock current = rof->lock_table[suiv].lock;
        if (is_interval_overlap(current.starting_offset, current.len, start, length)) {
            bool is_conflict = !is_owner(current, lowner) || !is_only_owner(current, lowner);
            if (is_reader && current.type == F_WRLCK) return true;
            if (!is_reader && is_conflict) return true;
        }
        suiv = rof->lock_table[suiv].lock.next_lock;
    }
    return false;
}

/**

    Retourne l'index dans la table des locks du premier lock dont [lowner] est l'unique propiétaire et qui
    chevauche à gauche selon [start] et [length] et dont le lock type = [lock_type]
*/
static int index_only_owner_lock_left_overlap(rl_descriptor rd, owner lowner, short lock_type, off_t start, off_t length) {
    rl_open_file* rof = rd.f;
    int suiv = rof->first;
    while(suiv >= 0) {
        rl_lock current = rof->lock_table[suiv].lock;
        bool is_owner_alone = is_only_owner(current, lowner);
        if (is_left_interval_overlap(current.starting_offset, current.len, start, length) && is_owner_alone && current.type == lock_type) {
            return suiv;
        }
        suiv = rof->lock_table[suiv].lock.next_lock;
    }
    return -1;
}

/**

    Retourne l'index dans la table des locks du premier lock dont [lowner] est l'unique propiétaire et qui
    chevauche à gauche selon [start] et [length]
*/
static int index_only_owner_lock_right_overlap(rl_descriptor rd, owner lowner, short lock_type, off_t start, off_t length) {
    rl_open_file* rof = rd.f;
    int suiv = rof->first;
    while(suiv >= 0) {
        rl_lock current = rof->lock_table[suiv].lock;
        bool is_owner_alone = has_one_owner(current) && is_owner(current, lowner);
        if (is_right_interval_overlap(current.starting_offset, current.len, start, length) && is_owner_alone && current.type == lock_type) {
            return suiv;
        }
        suiv = rof->lock_table[suiv].lock.next_lock;
    }
    return -1;
}

/**

    Retourne l'index dans la table des locks du premier lock dont [lowner] est l'unique propiétaire et qui
    est inclue dans la region de [start] ---- [start + length]
*/
static int index_only_owner_lock_included(rl_descriptor rd, owner lowner, short lock_type, off_t start, off_t length) {
    rl_open_file* rof = rd.f;
    int suiv = rof->first;
    while(suiv >= 0) {
        rl_lock current = rof->lock_table[suiv].lock;
        bool is_owner_alone = has_one_owner(current) && is_owner(current, lowner);
        if (is_interval_included(start, length, current.starting_offset, current.len) && 
            is_owner_alone && current.type == lock_type
        ) {
            return suiv;
        }
        suiv = rof->lock_table[suiv].lock.next_lock;
    }
    return -1;
}


/**
    Etend la region de lock existant ou cree le lock
*/
static int add_lock_type(rl_descriptor rd, owner lowner, const struct flock* const lock, bool is_blocking) {
    int left_lock_index = index_only_owner_lock_left_overlap(rd, lowner, lock->l_type, lock->l_start, lock->l_len);
    int right_lock_index = index_only_owner_lock_right_overlap(rd, lowner, lock->l_type, lock->l_start, lock->l_len);
    int inclued_index_lock = index_only_owner_lock_included(rd, lowner, lock->l_type, lock->l_start, lock->l_len);
    int free_square = index_lock_libre(rd.f);
    if (free_square == LOCK_FULL) {
        return -1;
    }
    if (left_lock_index == -1 && right_lock_index == -1 && inclued_index_lock == -1) { // Aucun lock a étendre
        
        rl_lock new_lock = {
            .next_lock = -1, 
            .starting_offset = lock->l_start, 
            .len = lock->l_len,
            .type = lock->l_type, 
            .nb_owners = 1
        };
        new_lock.lock_owners[0] = lowner;
        add_lock(rd.f, new_lock, free_square, is_blocking);
        return 0;
    }

    if (inclued_index_lock != -1) {
        rl_sem_lock* middlelock = rd.f->lock_table + inclued_index_lock;
        middlelock->lock.starting_offset = lock->l_start;
        middlelock->lock.len = lock->l_len;
        return 0;
    }

    if (left_lock_index != -1) {
        rl_sem_lock* left_lock = rd.f->lock_table + left_lock_index;
        off_t farest = max((left_lock->lock.starting_offset + left_lock->lock.len), (lock->l_start + lock->l_len));
        left_lock->lock.starting_offset = min(left_lock->lock.starting_offset, lock->l_start);
        left_lock->lock.len = farest - (left_lock->lock.starting_offset);
    }
    if (right_lock_index != -1) {
        rl_sem_lock* right_lock = rd.f->lock_table + right_lock_index;
        off_t farest = max((right_lock->lock.starting_offset + right_lock->lock.len), (lock->l_start + lock->l_len));
        right_lock->lock.starting_offset = min(right_lock->lock.starting_offset, lock->l_start);
        right_lock->lock.len = farest - (right_lock->lock.starting_offset);
    }

    if (left_lock_index != -1 && right_lock_index != -1) {
        rl_sem_lock* left_lock = rd.f->lock_table + left_lock_index;
        rl_sem_lock* right_lock = rd.f->lock_table + right_lock_index;
        left_lock->lock.starting_offset = min(left_lock->lock.starting_offset, right_lock->lock.starting_offset);
        left_lock->lock.len = (right_lock->lock.starting_offset + right_lock->lock.len) - left_lock->lock.starting_offset;
        rm_lock(rd.f, right_lock_index);
    }

    return 0;
}



static void delete_owner_rl_lock(rl_open_file* rof, int current_lock_index, owner o) {
    rl_sem_lock* ptr_lock =  rof->lock_table + current_lock_index;
    rl_lock lock = rof->lock_table[current_lock_index].lock;
    int index_owner = owner_index(lock, o);
    if (index_owner >= 0) {
        if (has_one_owner(lock)) {
            rm_lock(rof,  current_lock_index);
        } else {
            delete_owner_index(&rof->lock_table[current_lock_index].lock, index_owner, rof->lock_table[current_lock_index].lock.nb_owners);
            rof->lock_table[current_lock_index].lock.nb_owners -= 1;
        }
    }
    // Verifier que ce n'est pas le seul lock sur le ficher ...
    if (ptr_lock->lock.next_lock < 0) return;
    else {
        int next_index = ptr_lock->lock.next_lock;
        // rl_lock next_lock = rof->lock_table[next_index];
        delete_owner_rl_lock(rof, next_index, o);
        return;
    } 
}


/**
    Supprime le owner [o] de la liste des owner du [rl_lock]
    Si c'etait le seul proprietaire, supprime le [rl_lock]
*/
static void delete_owner_rl_openfile(rl_open_file* rof, owner o) {
    if (rof->first == NOT_USED_SQUARE) return;
    delete_owner_rl_lock(rof, rof->first, o);
}

static int add_owner(rl_lock *lock, owner own) {
    if(lock->nb_owners < NB_OWNERS) {
        lock->lock_owners[lock->nb_owners] = own;
        lock->nb_owners++;
        return 0;
    } else {
        return OWNER_FULL; //dans le cas ou le tableau d'owner est plein on renvoie une erreur
    }
}

static int is_opener(rl_open_file *rof, owner o) {
    for (int i = 0; i < rof->descripteur_ouvert; i++) {
        if(eq_owner(rof->opener[i], o)) {
            return i;
        }
    }
    return OPENER_NOT_FOUND;
}

static void delete_opener(rl_open_file *rof, owner o) {
    int index = is_opener(rof,o);
    if(index != OPENER_NOT_FOUND) {
        for (int i = index + 1; i < rof->descripteur_ouvert; i++) {
            rof->opener[i-1] = rof->opener[i];
        }
        rof->descripteur_ouvert--;
    }
}


static int add_opener(rl_open_file *rof, owner o, bool wait) {

    if (wait) {
        sem_wait(&rof->semaphore);
    }
    
    int res;
    if(rof->descripteur_ouvert < NB_OPENER) {
        rof->opener[rof->descripteur_ouvert] = o;
        rof->descripteur_ouvert++;
        res = 0;
    } else {
        res = OPENER_FULL;
    } 

    if (wait) {
        sem_post(&rof->semaphore);
    }
    
    return res;  
}

static int add_opener_tab(rl_open_file *rof, owner o[], int len, bool wait) {
    if(rof->descripteur_ouvert + len < NB_OPENER) {
        for (int i = 0; i < len; i++) {
            add_opener(rof, o[i], wait);
        }
        return 0;
    } else {
        return OPENER_FULL;
    }
}

static int add_opener_fils(rl_open_file *rof, pid_t parent_proc, pid_t fils_proc, bool wait) {
    owner tmp[NB_OPENER];
    int len = 0;
    for (int i = 0; i < rof->descripteur_ouvert; i++) {
        if(rof->opener[i].proc == parent_proc) {
            owner fils = {.des = rof->opener[i].des, .proc = fils_proc};
            tmp[len] = fils;
            len++;
        }
    }
    if(len > 0) {
        return add_opener_tab(rof, tmp, len, wait);
    } else {
        return OPENER_NOT_FOUND;
    }
}



static char* rl_formatted_object(struct stat statbuf, bool use_malloc) {
    const char* env_value = getenv(RL_PREFIX_OBJ);
    env_value = env_value != NULL ? env_value : RL_PREFIX_DEFAULT;
    int char_len = snprintf(NULL, 0, RL_SHM_FORMAT, env_value, (long) statbuf.st_dev, (long) statbuf.st_ino);
    char* obj_name;
    if (use_malloc) {
        obj_name = malloc(char_len + 1);
        if (obj_name == NULL) return NULL;
    } else {
        obj_name = mmap(NULL, char_len + 1, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
        if (obj_name == (void *) -1) return NULL;
    }

    sprintf(obj_name, RL_SHM_FORMAT, env_value, (long) statbuf.st_dev, (long) statbuf.st_ino);
    obj_name[char_len] = '\0';
    return obj_name;
}

int shm_open_formated(struct stat statbuf, int oflag, mode_t mode) {

    const char* obj_name = rl_formatted_object(statbuf, true);
    if (!obj_name)
        return MALLOC_FAIL;
    int obj = shm_open(obj_name, oflag, mode);
    if (obj < 0) {
        perror("shm_open");
    }
    free((void *) obj_name);
    return obj;
}


/**
    retourne l'index du verrou si il existe ou RL_LOCK_NOT_FOUND
*/
// static int get_lock_index(rl_open_file *rof, const struct flock *lck) {
//     int suiv = rof->first;
//     while(suiv >= 0) {
//         if(rof->lock_table[suiv].lock.type == lck->l_type && 
//             rof->lock_table[suiv].lock.starting_offset == lck->l_start && 
//             rof->lock_table[suiv].lock.len == lck->l_len
//         ) {
//             return suiv;
//         } else {
//             suiv = rof->lock_table[suiv].lock.next_lock;
//         }
//     }
//     return RL_LOCK_NOT_FOUND;
// }

static int get_lock_index_only_owner(rl_open_file *rof, owner o, const struct flock*const lck) {
    int suiv = rof->first;
    while(suiv >= 0) {
        rl_lock current = rof->lock_table[suiv].lock;
        if( 
            current.starting_offset == lck->l_start && 
            current.len == lck->l_len &&
            is_only_owner(current, o)
            ) {
            return suiv;
        } else {
            suiv = rof->lock_table[suiv].lock.next_lock;
        }
    }
    return RL_LOCK_NOT_FOUND;
}
/**
    retourne l'index du verrou sans compare le type du verrou si il existe ou RL_LOCK_NOT_FOUND
*/
static int get_lock_index_region(rl_open_file* rof, const struct flock*const lck) {
    int suiv = rof->first;
    while(suiv >= 0) {
        if(
            rof->lock_table[suiv].lock.starting_offset == lck->l_start && 
            rof->lock_table[suiv].lock.len == lck->l_len
        ) {
            return suiv;
        } else {
            suiv = rof->lock_table[suiv].lock.next_lock;
        }
    }
    return RL_LOCK_NOT_FOUND;
}

size_t nb_locks_on_file(rl_descriptor rd) {
    size_t n = 0;
    int suiv = rd.f->first;
    while(suiv >= 0) {
        n += 1;
        suiv = rd.f->lock_table[suiv].lock.next_lock;
    }
     
    return n;
}


static int index_lock_libre(rl_open_file *rof) { //renvoie l'indice de la premiere case du tableau lock_table libre
    for(int i = 0; i < NB_LOCKS; i++) {
        if(rof->lock_table[i].lock.next_lock == -2)
            return i;
    }
    return LOCK_FULL;
}

static lock_array_t get_overlapping_lock(rl_open_file* rlof, const struct flock* const lock) {
    lock_array_t array;
    size_t count = 0;
    int suiv = rlof->first;
    while(suiv >= 0) {
        rl_lock rlock = rlof->lock_table[suiv].lock;
        if (
            is_interval_included(rlock.starting_offset, rlock.len, lock->l_start, lock->l_len) ||
            is_interval_overlap(rlock.starting_offset, rlock.len, lock->l_start, lock->l_len)
        ) {
            array.locks_index[count] = suiv;
            count += 1;
        }
        suiv = rlof->lock_table[suiv].lock.next_lock;
    }
    array.count = count;
    return array;
}

static int check_proc(rl_descriptor lfd, int lock_index) {
    int res = 0;
    for(int i = 0;i < lfd.f->lock_table[lock_index].lock.nb_owners; i++) {
        int est_vivant = kill(lfd.f->lock_table[lock_index].lock.lock_owners[i].proc,0);
        if(est_vivant != 0) { // si est_vivant != 0 -> processus est mort
            res = 1;
            if(has_one_owner(lfd.f->lock_table[lock_index].lock)) {
                rm_lock(lfd.f,lock_index);
            } else {
                delete_owner_index(&lfd.f->lock_table[lock_index].lock,i,lfd.f->lock_table[lock_index].lock.nb_owners);
            }
        }
    }
    return res;
}


static int remove_dead_proc(rl_descriptor lfd, lock_array_t conflict) {
    int res = 0;
    for(int i = 0; i < conflict.count; i++) {
        int rem = check_proc(lfd,conflict.locks_index[i]);
        if(rem == 1) {
            res = 1;
        }
    }
    return res;
}



static void add_lock(rl_open_file *rof, rl_lock loc, int index_add, bool is_blocking) {
    int suiv = rof->first;
    if(suiv == -2) {
        rof->lock_table[index_add].lock = loc;
        rof->first = index_add;
    } else {
        while(suiv != -1) {
            if(rof->lock_table[suiv].lock.next_lock == -1) {
                rof->lock_table[index_add].lock = loc;
                sem_init(&rof->lock_table[index_add].writer, SEM_PROC_SYNC, 1); 
                rof->lock_table[suiv].lock.next_lock = index_add;

                if (!is_blocking) break;

                
                break;
            } else {
                suiv = rof->lock_table[suiv].lock.next_lock;
            }
        }
    }   
}


static void remove_rl_all_files(rl_descriptor rd) {
    int rem = 0;
    for (int i = 0; i < rl_all_files.nb_files; i++) {
        if( rl_all_files.tab_open_files[i] == rd.f && rem == 0) {

            // munmap( (void *) rd.shm_name, strlen(shm_name) + 1);
            rl_all_files.tab_open_files[i] = NULL;
            rem = 1;
        } else if (rem == 1) {
            rl_all_files.tab_open_files[i-1] = rl_all_files.tab_open_files[i];
        }
    }
    rl_all_files.nb_files--;
}






rl_descriptor rl_open(const char *path, int oflag, ...) {

    va_list ptr;
    va_start(ptr, oflag);
    int mode_flag = va_arg(ptr, int);
    va_end(ptr);
    int fd = open(path, oflag, mode_flag);

    rl_descriptor desc = {.d = fd, .f = NULL};

    if(fd == -1) { // si l'ouverture echoue
        return desc;
    }
    struct stat statbuf; // on recupere les caracteristique du fichier
    int st = fstat(fd, &statbuf);
    if(st == -1) {
        perror("fstat");
        return desc;
    }

    int shm_op = shm_open_formated(statbuf, O_CREAT| O_RDWR | O_EXCL, S_IWUSR | S_IRUSR); // on crée le shared memory object

    if (shm_op == MALLOC_FAIL) {
        perror("malloc");
        return desc;
    } 
    
    if (shm_op < 0 && errno != EEXIST) { 
        perror("shm_open");
        return desc;
    } 
    
    if (shm_op < 0 && errno == EEXIST) { // Shared object exite deja
        
        shm_op = shm_open_formated(statbuf, O_RDWR, S_IWUSR | S_IRUSR);
        // Si l'objet existe deja (statbuf.st_size) == sizeof(rl_open_file)
        rl_open_file *op_file = mmap(NULL, sizeof(rl_open_file) , PROT_READ | PROT_WRITE, MAP_SHARED, shm_op, 0);
        if ( op_file == MMAPP_ERROR_VALUE ) {
            fflush(stdout);
            perror("mmap");
            return desc;
        }
        owner proc_acc = {.des = fd, .proc = getpid()};
        if(add_opener(op_file, proc_acc, true) == OPENER_FULL) {   
            desc.d = -1;
            close(fd);
            return desc;
        }
        desc.shm_name = rl_formatted_object(statbuf, true);
        desc.f = op_file;
        //rl_all_files.nb_files++;
        return desc;


    } else { 
        // Le fichier n'existe pas on le cree et on le "ftruncate" avec sizeof rl_open_file
        off_t file_len = sizeof(rl_open_file);
        int truct = ftruncate(shm_op,file_len ); 
        if (truct < 0) {
            perror("ftruncate");
            return desc;
        }

        rl_open_file *op_file = mmap(NULL, file_len, PROT_READ | PROT_WRITE, MAP_SHARED, shm_op, 0);
        
        if ( op_file == MMAPP_ERROR_VALUE ) {
            perror("mmap");
            return desc;
        }

        sem_init(&op_file->semaphore, SEM_PROC_SYNC, 1);

        const char* shared_name = rl_formatted_object(statbuf, true);
        desc.shm_name = shared_name;
        owner proc_acc = {.des = fd, .proc = getpid()};
        op_file->opener[0] = proc_acc;
        op_file->descripteur_ouvert = 1;

        op_file->first = NOT_USED_SQUARE;
        for(int i = 0; i < NB_LOCKS; i++) {
            op_file->lock_table[i].lock.next_lock = NOT_USED_SQUARE; // on initialise les rl_lock avec next_lock -2
        }

        desc.f = op_file;

        rl_all_files.tab_open_files[rl_all_files.nb_files] = op_file;
        rl_all_files.nb_files++;
        return desc;

    }
    /*
        FILTRER LES FLAGS POUR METTRE LES BON FLAGS POUR MMAP ET PROJECTION MEMOIRE
    */

}



int rl_close(rl_descriptor lfd) {
    sem_wait(&lfd.f->semaphore);
    int ferme = close(lfd.d);
    if(ferme < 0) {
        perror("close");
        sem_post(&lfd.f->semaphore);
        return -1;
    }

    owner lfd_owner = { .proc = getpid(), .des = lfd.d };
    /* SUPPRIMER TOUT LES VERROUS ASSOCIÉ A LFD_OWNER */
    /*for (size_t index = 0; index < NB_FILES; index += 1) {
        delete_owner_rl_openfile(rl_all_files.tab_open_files[index], lfd_owner);
    }*/
    // puts(lfd.f->shm_name);
    printf("\n Close Start \n");
    printf("je vais supp les lock\n");
    // printf("file: %p, name = \"%s\", pid = %d, fd : %d\n", lfd.f, lfd.f->shm_name, getpid(), lfd.d);
    delete_owner_rl_openfile(lfd.f, lfd_owner);
    printf("j'ai supp les lock\n");
    delete_opener(lfd.f,lfd_owner);     

    // printf("filename : %s opened descriptors: %d\n", lfd.f->shm_name, lfd.f->descripteur_ouvert);
    printf("\n Close End \n");
    printf("open desc = %d\n", lfd.f->descripteur_ouvert);
    printf("SHM_NAME = %s\n", lfd.shm_name);
    if(lfd.f->descripteur_ouvert == 0 && lfd.shm_name != NULL) {
        write(STDOUT_FILENO, "727\n", 4);
        sem_destroy(&lfd.f->semaphore);
        int unl = shm_unlink(lfd.shm_name);
        if(unl == -1) {
            sem_post(&lfd.f->semaphore);
            perror("shm_unlink");
            return -1;
        }
        remove_rl_all_files(lfd);
        // munmap((void *) lfd.f->shm_name, strlen(lfd.f->shm_name) + 1);
    }

    sem_post(&lfd.f->semaphore);

    return 0;
}


/**
    struct flock.type = F_RDLCK
    + [owner] à un lock correspondant sur la region exacte du lock

    Suppose que si plusieurs locks sont posés sur une région, ils sont tous en lecture sinon [rl_fcntl] aurait échoué puisque'il y aurait un lock en ecriture
    Pas forcemenet vrai a cause des fork
*/
static int rl_fcntl_read_owner(rl_descriptor lfd, owner lfd_owner, int lock_index, bool is_blocking) {
    rl_sem_lock* owner_lock = lfd.f->lock_table + lock_index; 
    if ( has_one_owner(owner_lock->lock) && F_WRLCK == owner_lock->lock.type) {
        // Si le seul propriétaire détient un lock en ecriture, on le transforme juste en lecture
        /// TODO: Reveilleur les processus blocker par le lock est passé en lecture
        owner_lock->lock.type = F_RDLCK;
        if (is_blocking) {
            sem_post(&owner_lock->writer);
        }
       
    } 
    // Dans les autres cas
    // - Si n'est pas le seul propriétaire c'est que le lock n'est pas en ecriture
    // - Si son lock est différent de F_WRLCK c'est qu'il est en lecture (dans le contexte de la fonction)
    return 0;
}


/**

    Tente d'ajouter un propriétaire (owner) sur un verrou existant
    Le verrou doit etre en en lecture
*/
static int rl_fcntl_read_add_owner_existing_lock(rl_descriptor lfd, owner lfd_owner, int lock_index, bool is_blocking) {
    rl_lock lock = lfd.f->lock_table[lock_index].lock;
    if (has_one_owner(lock) && is_owner(lock, lfd_owner)) { // SEUL PROPRIETAIRE D'un verrou en ecriture
        return rl_fcntl_read_owner(lfd, lfd_owner, lock_index, is_blocking);
    }
    if (is_owner(lock, lfd_owner)) {
        int free_square = index_lock_libre(lfd.f);
        if (free_square == RL_LOCK_NOT_FOUND) {
            return -1;
        }
        delete_owner_rl_lock(lfd.f, lock_index, lfd_owner);
        rl_lock new_lock = {
            .next_lock = -1, 
            .starting_offset = lock.starting_offset,
            .len = lock.len,
            .type = F_RDLCK, 
            .nb_owners = 1
        };
        new_lock.lock_owners[0] = lfd_owner;
        add_lock(lfd.f, new_lock, free_square, is_blocking);
        return 0;
    }
    int added = add_owner(&lfd.f->lock_table[lock_index].lock, lfd_owner);
    if (added == OWNER_FULL) {
        perror("erreur, le verrou n'a pas pu etre attribué car il est plein");
        return -1;
    } else {
        return 0;
    }
}

/**
    Context
    [lock_index] indice dans le tableau du rl_lock dont [owner] est l'unique propriétaire
*/
static int rl_fcntl_write_add_owner_existing_read_lock(rl_descriptor lfd, owner lfd_owner, int lock_index) {
    rl_sem_lock* lock = lfd.f->lock_table + lock_index;
    if (lock->lock.type == F_WRLCK) return 0; // On essaie de mattre un lock en ecriture sur un lock en ecriture qui existe deja dont on est le propriétaire
    /// TODO: Semaphore gestion
    lock->lock.type = F_WRLCK;
    return 0;
}

static int rl_fcntl_unlock_existing_lock(rl_descriptor lfd, owner lfd_owner, int lock_index) {
    /// TODO: Unlock sem selon ecrivant ou lecteur
    rl_sem_lock* lock = lfd.f->lock_table + lock_index;
    int index_owner = owner_index(lock->lock, lfd_owner); 
    if (index_owner == OWNER_NOT_FOUND) return 0;
    delete_owner_index(&lock->lock, index_owner, lock->lock.nb_owners);
    if (has_one_owner(lock->lock)){
        rm_lock(lfd.f,lock_index);
    } else {
        lock->lock.nb_owners -= 1;
    }
    return 0;
}

static bool can_add_n_lock(rl_descriptor rd, size_t n) {
    return n + nb_locks_on_file(rd) < NB_LOCKS;
}

static int rl_fcntl_unlock_segment(rl_descriptor lfd, int cmd, owner o, const struct flock*const lock_to_remove) {
    int suiv = lfd.f->first;
        while(suiv >= 0) {
        rl_lock* lock = &(lfd.f->lock_table + suiv)->lock;

        if (is_owner(*lock, o)) {
            // int index_owner = owner_index(*lock, o);
            bool is_inclueded = is_interval_included(lock->starting_offset, lock->len, lock_to_remove->l_start, lock_to_remove->l_len);
            size_t n_lock = nb_locks_on_file(lfd);
            size_t lock_2_add = is_only_owner(*lock, o) ? 1 : 2; // Si c'est l'unique owner le locke sera supprimer pour en cree 2 (2 - 1) sinon (2 - 0) 
            bool can_add_locks = (n_lock + lock_2_add) < NB_LOCKS;
            bool can_add_one_lock = can_add_n_lock(lfd, 1);
            bool does_need_add_lock = !is_only_owner(*lock, o);
            if (is_inclueded) {
                if (!can_add_locks) goto next;
                delete_owner_rl_lock(lfd.f, suiv, o);
                struct flock first_part_lock = {
                    .l_start = lock->starting_offset, 
                    .l_len = (lock_to_remove->l_start - lock->starting_offset), 
                    .l_type = lock->type
                };
                struct flock second_part_lock = {
                    .l_start = lock_to_remove->l_start + lock_to_remove->l_len, 
                    .l_len = lock->len - (first_part_lock.l_len + lock_to_remove->l_len),
                    .l_type = lock->type
                };
                tmp_fcntl_aux(lfd, cmd, &first_part_lock);
                tmp_fcntl_aux(lfd, cmd, &second_part_lock);
                goto next;
            } else if (is_left_interval_overlap(lock->starting_offset, lock->len, lock_to_remove->l_start, lock->len)) {
                if (does_need_add_lock && !can_add_one_lock) goto next;
                delete_owner_rl_lock(lfd.f, suiv, o);
                struct flock first_part_lock = {
                    .l_start = lock->starting_offset, 
                    .l_len = (lock_to_remove->l_start - lock->starting_offset), 
                    .l_type = lock->type
                };
                tmp_fcntl_aux(lfd, cmd, &first_part_lock);

            } else if (is_right_interval_overlap(lock->starting_offset, lock->len, lock_to_remove->l_start, lock->len)) {
                if (does_need_add_lock && !can_add_one_lock) goto next; 
                delete_owner_rl_lock(lfd.f, suiv, o);
                off_t new_start = lock_to_remove->l_start + lock_to_remove->l_len;
                struct flock second_part_lock = {
                    .l_start = new_start, 
                    .l_len = (lock->starting_offset + lock->len) - (new_start),
                    .l_type = lock->type
                };
                tmp_fcntl_aux(lfd, cmd, &second_part_lock);
            }
        }
        next:
            suiv = lfd.f->lock_table[suiv].lock.next_lock;
    }
    return 0;
}

int rl_fcntl_lock_conflict(rl_descriptor rlfd, int cmd, owner o, struct flock* lock, bool is_blocking) {
    bool is_reader = lock->l_type == F_RDLCK;
    lock_array_t conficts = get_overlapping_lock(rlfd.f, lock);
    /*for(int i = 0; i < conficts.count; i++) {
        printf("%d\n",conficts.locks_index[i]);
    }*/
    int supp = remove_dead_proc(rlfd, conficts);
    if(supp) {
        tmp_fcntl_aux(rlfd,cmd,lock);
    } else {
        if (!is_blocking) return -1;
        for (size_t i = 0; i < conficts.count; i++) {
            rl_sem_lock* slock = rlfd.f->lock_table + conficts.locks_index[i];
            bool is_conflict = !is_owner(slock->lock, o) || !is_only_owner(slock->lock, o);
            if (is_reader && slock->lock.type == F_WRLCK && is_conflict) {
                sem_wait(&slock->writer);
            }
            if (!is_reader && is_conflict) {
                sem_wait(&slock->writer);
            }
        }
    }

    return 0;
}

int tmp_fcntl_aux(rl_descriptor lfd, int cmd, struct flock* lock) {
    owner lfd_owner = { .proc = getpid(), .des = lfd.d };
    bool is_blocking = cmd == F_SETLKW;
    switch (lock->l_type) {
        case F_RDLCK: {
            bool has_conflict = has_set_conflicts_lock(lfd, lfd_owner, lock->l_start, lock->l_len, true);
            if (has_conflict) return rl_fcntl_lock_conflict(lfd, cmd, lfd_owner, lock, is_blocking);
            int existing_lock_index = get_lock_index_region(lfd.f, lock);
            if (existing_lock_index != RL_LOCK_NOT_FOUND) {
                return rl_fcntl_read_add_owner_existing_lock(lfd, lfd_owner, existing_lock_index, is_blocking);   
            } else {
               return add_lock_type(lfd, lfd_owner, lock, is_blocking);
            }
        }
        case F_WRLCK: {
            bool cannot_set_lock = has_set_conflicts_lock(lfd, lfd_owner, lock->l_start, lock->l_len, false);
            if (cannot_set_lock) return rl_fcntl_lock_conflict(lfd, cmd, lfd_owner, lock, is_blocking);
            int existing_lock_index = get_lock_index_only_owner(lfd.f, lfd_owner, lock);
            if (existing_lock_index != RL_LOCK_NOT_FOUND) {
                return rl_fcntl_write_add_owner_existing_read_lock(lfd, lfd_owner, existing_lock_index);
            } else {
                return add_lock_type(lfd, lfd_owner, lock, is_blocking);
            }
        }
        case F_UNLCK: {
            int lock_index = get_lock_index_region(lfd.f, lock);
            if (lock_index != RL_LOCK_NOT_FOUND) {
                return rl_fcntl_unlock_existing_lock(lfd, lfd_owner, lock_index);
            } else {
                return rl_fcntl_unlock_segment(lfd, cmd, lfd_owner, lock);
            }
        }
        default:
            return -1;
    }
}

struct flock relative_to_seek_set(rl_descriptor rld, struct flock* lock) {
    struct flock copy = *lock;
    switch (lock->l_whence) {
        case SEEK_CUR: {
            off_t current_pose = lseek(rld.d, 0, SEEK_CUR);
            copy.l_start = copy.l_start + current_pose;
            copy.l_whence = SEEK_SET;
            break;
        }

        case SEEK_END: {
            off_t current_pose = lseek(rld.d, 0, SEEK_CUR);
            off_t end_position = lseek(rld.d, 0, SEEK_END);
            lseek(rld.d, current_pose, SEEK_SET);
            copy.l_start = copy.l_start + end_position;
            copy.l_whence = SEEK_SET;
            break;
        }
    }
    return copy; 
}

int rl_fcntl(rl_descriptor lfd, int cmd, struct flock* lock) {
    sem_wait(&lfd.f->semaphore);
    struct flock lock_copy = relative_to_seek_set(lfd, lock);
    int res = tmp_fcntl_aux(lfd, cmd, &lock_copy);
    sem_post(&lfd.f->semaphore);
   
    return res;
}

rl_descriptor rl_dup(rl_descriptor lfd) {
    int newd = dup(lfd.d);
    int len = strlen(lfd.shm_name);
    const char* shm_name_cpy = malloc(len + 1);
    strncpy( (char *) shm_name_cpy, lfd.shm_name, len);
    if(newd < 0) {
        rl_descriptor desc = {.d = newd, .f = lfd.f, .shm_name = shm_name_cpy};
        return desc;
    }
    owner lfd_owner = {.des = lfd.d, .proc = getpid()};
    owner new_owner = {.des = newd, .proc = getpid()};
    // int semvalue;
    // sem_getvalue(&lfd.f->semaphore, &semvalue);
    sem_wait(&lfd.f->semaphore);
    if(add_opener(lfd.f, new_owner, false) == OWNER_FULL) {
        rl_descriptor desc = {.d = -1, .f = NULL};
         sem_post(&lfd.f->semaphore);
        close(newd);
        return desc;
    }
    printf("dup fd: %d\n", newd);
    int suiv = lfd.f->first;
    while(suiv != -1 && suiv != -2) {
       //on parcours le tableau lock_owner
        int own = owner_index(lfd.f->lock_table[suiv].lock, lfd_owner);
        if(own != OWNER_NOT_FOUND) {
            int add_o = add_owner(&lfd.f->lock_table[suiv].lock, new_owner);
            if (add_o == OWNER_FULL) {
                puts("Un verrou n'a pas pu etre dupliqué");
            }
        }
        suiv = lfd.f->lock_table[suiv].lock.next_lock;
    }

    

    rl_descriptor new_rl_descriptor = { .d = newd, .f = lfd.f, .shm_name = shm_name_cpy };
    sem_post(&lfd.f->semaphore);
    return new_rl_descriptor;
}

rl_descriptor rl_dup2(rl_descriptor lfd, int newd) {
    int fd = dup2(lfd.d,newd);
    if(newd < 0 || fd < 0) {
        rl_descriptor desc = {.d = newd, .f = lfd.f};
        return desc;
    }
    owner lfd_owner = { .proc = getpid(), .des = lfd.d };
    owner new_owner = { .des = newd, .proc = getpid()};
    sem_wait(&lfd.f->semaphore);
    if(add_opener(lfd.f, new_owner, false) == OWNER_FULL) {
        rl_descriptor desc = {.d = -1, .f = NULL};
        close(newd);
        sem_post(&lfd.f->semaphore);
        return desc;
    }
    
    int suiv = lfd.f->first;
    
    while(suiv != -1 && suiv != -2) {
        for(int i = 0; i < lfd.f->lock_table[suiv].lock.nb_owners; i++) { //on parcours le tableau lock_owner
            int own = owner_index(lfd.f->lock_table[suiv].lock, lfd_owner);
            if(own != OWNER_NOT_FOUND) {
                int add_o = add_owner(&lfd.f->lock_table[suiv].lock,new_owner);
                if(add_o == OWNER_FULL) {
                    puts("Un verrou n'a pas pu etre dupliqué");
                }
            }
            suiv = lfd.f->lock_table[suiv].lock.next_lock;
        }
    }

    rl_descriptor new_rl_descriptor = { .d = newd, .f = lfd.f };
    sem_post(&lfd.f->semaphore);
    return new_rl_descriptor;
}


pid_t rl_fork() {
    pid_t proc = fork();
    if (proc == -1) {
        perror("fork");
        return proc;
    } 

    if (proc > 0) {return proc;}

    pid_t pid_parent = getppid();
    pid_t pid = getpid();
    printf("Fork enfant: pid: %d, proc: %d, parent: %d\n", getpid(), proc, pid_parent);
    for (int i = 0; i < rl_all_files.nb_files; i++) {
        if(add_opener_fils(rl_all_files.tab_open_files[i], pid_parent, pid, false) == -1) continue;
        int suiv = rl_all_files.tab_open_files[i]->first;
        while(suiv >= 0) {
            printf("suivant = %d\n", suiv);
            for(int j = 0; j < rl_all_files.tab_open_files[i]->lock_table[suiv].lock.nb_owners; j++) {
                if(rl_all_files.tab_open_files[i]->lock_table[suiv].lock.lock_owners[j].proc == pid_parent) {
                    owner fils = {.proc = pid, .des = rl_all_files.tab_open_files[i]->lock_table[suiv].lock.lock_owners[j].des};
                    int add_f = add_owner(&rl_all_files.tab_open_files[i]->lock_table[suiv].lock, fils);
                    if(add_f == OWNER_FULL) {
                        puts("Un verrou n'a  pas pu etre attribué au fils");
                    }
                }
            }
            suiv = rl_all_files.tab_open_files[i]->lock_table[suiv].lock.next_lock;
        }
    }
    
    return proc;
    
}


int rl_init_library() {
    rl_all_files.nb_files = 0;
    for(int i = 0; i < NB_FILES; i++) {
        rl_all_files.tab_open_files[i] = NULL;
    }
    return 0;
}


void print_rl_open_files(rl_open_file * rof) {
    printf("\nce rl_open_file a %d descripteur ouvert dessus\n", rof->descripteur_ouvert);
    if(rof->first == -2) {
        printf("\til n'a pas de verrou\n");
    } else {
        int suiv = rof->first;
        while(suiv >= 0) {
            printf("\til possede un verrou avec %ld owner, il est d'index %d \n",rof->lock_table[suiv].lock.nb_owners,suiv);
            printf("\tle verrou est de type %d, il commence sur l'octet %ld et va jusqu'a l'octet %ld \n", 
            rof->lock_table[suiv].lock.type,rof->lock_table[suiv].lock.starting_offset,rof->lock_table[suiv].lock.starting_offset + rof->lock_table[suiv].lock.len);
            for(int i = 0; i < rof->lock_table[suiv].lock.nb_owners; i++) {
                printf("\towner : proc=%d , des = %d \n",rof->lock_table[suiv].lock.lock_owners[i].proc,rof->lock_table[suiv].lock.lock_owners[i].des);
            }
            suiv = rof->lock_table[suiv].lock.next_lock;
        }
    }
}

void print_rl_all_files() { //fonction juste pour voir le contenue de rl_all_files pour faire des test
    printf("\n ****************************************\nil y a %d files\n",rl_all_files.nb_files);
    for(int i = 0; i < rl_all_files.nb_files; i++) {
        print_rl_open_files(rl_all_files.tab_open_files[i]);
    }
}
