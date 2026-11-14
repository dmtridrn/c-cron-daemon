#include "reseau.h"
#include "communication.h"
#include "parsing.h"
#include "operationsys.h"
#include "types.h"
#include "memoire.h" 
#include "executeur.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>  
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

// fonction pour lire les arguments d'une commande depuis un fichier argv
string_t* lire_arguments(const char *argv_path, u32 *argc){
    int fd = open(argv_path, O_RDONLY);
    if (fd < 0) {
        *argc = 0;
        return NULL;
    }
    *argc = read_u32(fd);
    if (*argc == 0) {
        close(fd);
        return NULL;
    }

    // met tous les args dans un tableau de string_t
    string_t *argv = malloc(*argc * sizeof(string_t));
    if (!argv) {
        close(fd);
        *argc = 0;
        return NULL;
    }

    for (u32 i = 0; i < *argc; i++) argv[i] = read_string(fd);
    
    close(fd);
    return argv;
}

// fonction pour vérifier si une tâche existe, et récupérer son chemin si besoin
static int verifier_tache_existe(const char *tasks_dir, u64 taskid, char **task_path_out, int fd_reply){
    size_t len = strlen(tasks_dir) + 50;
    char *path = allouer_chemin(len);

    if (!path){
        write_u16(fd_reply, ANSTYPE_ER);
        write_u16(fd_reply, ERRCODE_NF);
        return 0;
    }

    snprintf(path, len, "%s/%lu", tasks_dir, taskid);
    struct stat st;

    if(stat(path, &st) != 0 || !S_ISDIR(st.st_mode)){
        write_u16(fd_reply, ANSTYPE_ER);
        write_u16(fd_reply, ERRCODE_NF);
        free(path);
        return 0;
    }

    // si l'appelant veut recuperer le chemin
    if (task_path_out) *task_path_out = path;
    else free(path);

    return 1;
}

// fonction pour lire le timing d'une tâche depuis son répertoire
static timing_t lire_timing_tache(const char *task_path) {
    timing_t timing = {0, 0, 0}; // Timing par défaut (jamais) mais au cas où
    
    size_t len = strlen(task_path) + 20;
    char *timing_path = allouer_chemin(len);
    if (!timing_path) {
        return timing;
    }
    
    snprintf(timing_path, len, "%s/timing", task_path);
    int fd = open(timing_path, O_RDONLY);
    free(timing_path);
    
    if (fd >= 0) {
        timing = read_timing(fd);
        close(fd);
    }
    
    return timing;
}

// traite la requete LIST et envoie la réponse sur le pipe
void traiter_list(const char *tasks_dir, int fd_reply){
    DIR *dir = opendir(tasks_dir);
    if(!dir){
        perror("ouverture tasks LIST");
        write_u16(fd_reply, ANSTYPE_ER);
        write_u16(fd_reply, ERRCODE_NR);
        return;
    }
    // compte les tasks pour la taille du tableau après
    u32 count = 0;
    struct dirent *entry;

    while ((entry = readdir(dir))){
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        
        size_t len = strlen(tasks_dir) + strlen(entry->d_name) + 2;
        char *path = allouer_chemin(len);
        if (!path) continue;
        
        snprintf(path, len, "%s/%s", tasks_dir, entry->d_name);
        struct stat st;
        
        if (stat(path, &st) == 0 && S_ISDIR(st.st_mode) && est_numerique(entry->d_name)) count++;
        free(path);
    }
    //tab de tasks
    task_info_t *tasks = malloc(count * sizeof(task_info_t));
    if (!tasks) {
        closedir(dir);
        write_u16(fd_reply, ANSTYPE_ER);
        write_u16(fd_reply, ERRCODE_NR);
        return;
    }

    // revient au début du répertoire pour relire les tâches
    rewinddir(dir);
    u32 index = 0;
    while((entry = readdir(dir)) != NULL && index < count){
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        size_t len = strlen(tasks_dir) + strlen(entry->d_name) + 2;
        char *path = allouer_chemin(len);
        if (!path) continue;

        snprintf(path, len, "%s/%s", tasks_dir, entry->d_name);
        struct stat st;

        if (stat(path, &st) == 0 && S_ISDIR(st.st_mode) && est_numerique(entry->d_name)){
            tasks[index].taskid = strtoull(entry->d_name, NULL, 10); //unsigned long long pcq 64 bits équi à u64;
            
            // Lire le timing avec la p'tite fonction
            tasks[index].timing = lire_timing_tache(path);
            
            // Lire la commande depuis l'arborescence
            size_t cmd_len = len+10;
            char *cmd_path = allouer_chemin(cmd_len);
            if (cmd_path) {
                snprintf(cmd_path, cmd_len, "%s/cmd", path);
                tasks[index].cmd = read_command(cmd_path);
                free(cmd_path);
            } else tasks[index].cmd = NULL;
            
            index++;
        }
        free(path);
    }
    closedir(dir);

    //on write tout ca dans le pipe
    write_u16(fd_reply, ANSTYPE_OK);
    write_u32(fd_reply, index);
    
    for (u32 i = 0; i < count; i++) {
        write_u64(fd_reply, tasks[i].taskid);
        write_timing(fd_reply, &tasks[i].timing);
        if (tasks[i].cmd) {
            write_command(fd_reply, tasks[i].cmd);
            free_command(tasks[i].cmd);
            free(tasks[i].cmd);
        }
    }
    
    free(tasks);
}

// fonction plus propre grace a verifier_tache_existe
void traiter_times_exitcodes(const char *tasks_dir, u64 taskid, int fd_reply){
    char *task_path = NULL;
    
    if (!verifier_tache_existe(tasks_dir, taskid, &task_path, fd_reply)) return;

    // on lit le fichier times-exit codes
    size_t len = strlen(task_path) + 30;
    char *path = allouer_chemin(len);
    snprintf(path, len, "%s/times-exitcodes", task_path);
    free(task_path); // on n'a plus besoin du chemin du répertoire

    u32 count = 0;
    time_exitcode_t *entries = read_time_exitcodes(path, &count);

    // envoyer la réponse OK 
    write_u16(fd_reply, ANSTYPE_OK);
    write_u32(fd_reply, count);

    // on envoie chaque entrée
    for(u32 i = 0; i< count ; i ++){
        write_i64(fd_reply, entries[i].time);
        write_u16(fd_reply, entries[i].exitcode);
    }

    if(entries)free(entries);
    free(path);
}

// fonction pour lire un fichier de sortie (stdout ou stderr)
static string_t lire_fichier_sortie(const char *filepath) {
    string_t result = {0, NULL};
    
    int fd = open(filepath, O_RDONLY);
    if (fd < 0) return result;
    
    // obtenir la taille du fichier
    struct stat st;
    if (fstat(fd, &st) != 0) {
        close(fd);
        return result;
    }
    
    result.length = st.st_size;
    result.data = malloc(result.length);
    if (!result.data) {
        close(fd);
        result.length = 0;
        return result;
    }
    
    ssize_t nb = read(fd, result.data, result.length);
    close(fd);
    
    if (nb != (ssize_t)result.length) {
        free(result.data);
        result.data = NULL;
        result.length = 0;
    }
    
    return result;
}

// fonction static pour traiter stderr ou stdout
static void traiter_std_commun(const char *tasks_dir, u64 taskid, int fd_reply, const char *nom_fichier) {
    char *task_path = NULL;
    if (!verifier_tache_existe(tasks_dir, taskid, &task_path, fd_reply)) return;
    
    // construire le chemin vers stdout ou stderr
    size_t len = strlen(task_path) + strlen(nom_fichier) + 2; // +1 pour /, +1 pour \0
    char *path = allouer_chemin(len);
    snprintf(path, len, "%s/%s", task_path, nom_fichier);
    free(task_path);
    
    // vérifier si le fichier existe
    struct stat st;
    if (stat(path, &st) != 0) {
        write_u16(fd_reply, ANSTYPE_ER);
        write_u16(fd_reply, ERRCODE_NR);
        free(path);
        return;
    }
    
    // lire le contenu
    string_t output = lire_fichier_sortie(path);
    free(path);
    
    if (!output.data) {
        write_u16(fd_reply, ANSTYPE_ER);
        write_u16(fd_reply, ERRCODE_NR);
        return;
    }
    
    // envoyer la réponse OK
    write_u16(fd_reply, ANSTYPE_OK);
    write_string(fd_reply, &output);
    
    free_string(&output);
}


void traiter_stdout(const char *tasks_dir, u64 taskid, int fd_reply) {
traiter_std_commun(tasks_dir, taskid, fd_reply, "stdout");
}

void traiter_stderr(const char *tasks_dir, u64 taskid, int fd_reply) {
    traiter_std_commun(tasks_dir, taskid, fd_reply, "stderr");
}


void traiter_remove(const char *tasks_dir, u64 taskid, int fd_reply){
    char *task_path = NULL;
    if (!verifier_tache_existe(tasks_dir, taskid, &task_path, fd_reply)) {
        return;
    }

    if(supprimer_repertoire_recursif(task_path) == -1){
        write_u16(fd_reply, ANSTYPE_ER);
        write_u16(fd_reply, ERRCODE_NR);
        free(task_path);
        return;
    }
    write_u16(fd_reply, ANSTYPE_OK);
    free(task_path);
}

static u64 generer_nouvel_id(const char *tasks_dir) {//static = private
    DIR *dir = opendir(tasks_dir);
    if (!dir) return 1;
    u64 max_id = 0;
    struct dirent *entry;
    
    while ((entry = readdir(dir)) != NULL) {
        if (est_numerique(entry->d_name)) {
            u64 id = strtoull(entry->d_name, NULL, 10);
            if (id > max_id) {
                max_id = id;
            }
        }
    }
    closedir(dir);
    return max_id + 1;
}

void traiter_create(const char *tasks_dir, int fd_req, int fd_reply){
    timing_t timing = read_timing(fd_req);
    u32 argc = read_u32(fd_req);
    string_t *argv = malloc(argc * sizeof(string_t));
    for (u32 i = 0; i < argc; i++) {
        argv[i] = read_string(fd_req);
    }
    u64 id = generer_nouvel_id(tasks_dir);
    size_t task_len = strlen(tasks_dir) + 30;
    //tasks/ID
    char *task_path = allouer_chemin(task_len);
    if (!task_path) {
        for (u32 i = 0; i < argc; i++) free(argv[i].data);
        free(argv);
        write_u16(fd_reply, ANSTYPE_ER);
        return;
    }
    snprintf(task_path, task_len, "%s/%lu", tasks_dir, id);
    mkdir(task_path, 0755);
    //tasks/ID/cmd
    size_t cmd_len = strlen(task_path) + 10;
    char *cmd_path = allouer_chemin(cmd_len);
    if (!cmd_path) {
        free(task_path);
        for (u32 i = 0; i < argc; i++) free(argv[i].data);
        free(argv);
        write_u16(fd_reply, ANSTYPE_ER);
        return;
    }
    snprintf(cmd_path, cmd_len, "%s/cmd", task_path);
    mkdir(cmd_path, 0755);

    //type
    size_t type_len = strlen(cmd_path) + 10;
    char *type_path = allouer_chemin(type_len);
    snprintf(type_path, type_len, "%s/type", cmd_path);
    int fd_type = open(type_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    write_u16(fd_type, CMD_TYPE_SIMPLE);  // 0x5349
    close(fd_type);
    free(type_path);

    //cmd/argv
    size_t argv_len = strlen(cmd_path) + 10;
    char *argv_path = allouer_chemin(argv_len);
    snprintf(argv_path, argv_len, "%s/argv", cmd_path);
    int fd_argv = open(argv_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    write_u32(fd_argv, argc);
    for (u32 i = 0; i < argc; i++) {
        write_u32(fd_argv, argv[i].length);
        write(fd_argv, argv[i].data, argv[i].length);
    }
    close(fd_argv);
    free(argv_path);
    free(cmd_path);

    //timing
    size_t timing_len = strlen(task_path) + 15;
    char *timing_path = allouer_chemin(timing_len);
    snprintf(timing_path, timing_len, "%s/timing", task_path);
    int fd_timing = open(timing_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    write_timing(fd_timing, &timing);
    close(fd_timing);
    free(timing_path);

    //time exitcode
    size_t te_len = strlen(task_path) + 20;
    char *te_path = allouer_chemin(te_len);
    snprintf(te_path, te_len, "%s/times-exitcodes", task_path);
    int fd_te = open(te_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    close(fd_te);
    free(te_path);
    free(task_path);

    write_u16(fd_reply, ANSTYPE_OK);
    write_u64(fd_reply, id);

    for (u32 i = 0; i < argc; i++) free(argv[i].data);
    free(argv);
}

// fonction static pour écrire une commande sur le disque
static int creer_commande_arbo(const char *path, command_t *cmd) {
    if (mkdir(path, 0755) < 0 && errno != EEXIST) {
        perror("mkdir command path");
        return -1;
    }

    size_t len = strlen(path) + 10;
    char *file_path = allouer_chemin(len);
    if (!file_path) return -1;

    snprintf(file_path, len, "%s/type", path);
    int fd = open(file_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        free(file_path);
        return -1;
    }
    write_u16(fd, cmd->type);
    close(fd);

    if (cmd->type == CMD_TYPE_SIMPLE) {
        snprintf(file_path, len, "%s/argv", path);
        fd = open(file_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
            free(file_path);
            return -1;
        }
        write_u32(fd, cmd->args.argc);
        for (u32 i = 0; i < cmd->args.argc; i++) {
            write_string(fd, &cmd->args.argv[i]);
        }
        close(fd);
    } else {
        // commande composite
        for (u32 i = 0; i < cmd->nsub; i++) {
            char *sub_path = allouer_chemin(len + 10);
            if (sub_path) {
                snprintf(sub_path, len + 10, "%s/%u", path, i);
                if (creer_commande_arbo(sub_path, &cmd->subcmds[i]) < 0) {
                    free(sub_path);
                    free(file_path);
                    return -1;
                }
                free(sub_path);
            }
        }
    }

    free(file_path);
    return 0;
}

// traite la requete COMBINE et envoie la réponse sur le pipe
void traiter_combine(const char *tasks_dir, int fd_req, int fd_reply) {
    timing_t timing = read_timing(fd_req);
    u16 type = read_u16(fd_req);
    u32 num_tasks = read_u32(fd_req);
    
    // allouer un tableau pour les IDs des tâches à combiner
    u64 *task_ids = malloc(num_tasks * sizeof(u64));
    if (!task_ids) {
        write_u16(fd_reply, ANSTYPE_ER);
        write_u16(fd_reply, ERRCODE_NR);
        return;
    }

    // lire les IDs des tâches à combiner
    for (u32 i = 0; i < num_tasks; i++) task_ids[i] = read_u64(fd_req);

    // créer la nouvelle tâche combinée
    u64 new_id = generer_nouvel_id(tasks_dir);
    size_t len = strlen(tasks_dir) + 30;
    char *task_path = allouer_chemin(len);
    if (!task_path) {
        free(task_ids);
        write_u16(fd_reply, ANSTYPE_ER);
        write_u16(fd_reply, ERRCODE_NR);
        return;
    }

    // tasks/NEW_ID
    snprintf(task_path, len, "%s/%lu", tasks_dir, new_id);
    if (mkdir(task_path, 0755) < 0) {
        free(task_ids);
        free(task_path);
        write_u16(fd_reply, ANSTYPE_ER);
        write_u16(fd_reply, ERRCODE_NR);
        return;
    }

    // preparer les chemins pour les fichiers
    char *file_path = allouer_chemin(len + 20);
    char *cmd_path = allouer_chemin(len + 10);
    if (!file_path || !cmd_path) {
        free(task_ids);
        free(task_path);
        free(file_path);
        free(cmd_path);
        write_u16(fd_reply, ANSTYPE_ER);
        write_u16(fd_reply, ERRCODE_NR);
        return;
    }
    // créer le fichier timing
    snprintf(file_path, len + 20, "%s/timing", task_path);
    int fd = open(file_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    // écrire le timing
    write_timing(fd, &timing);
    close(fd);

    // créer le fichier times-exitcodes vide
    snprintf(file_path, len + 20, "%s/times-exitcodes", task_path);
    fd = open(file_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    close(fd);
    
    // créer le répertoire cmd
    snprintf(cmd_path, len + 10, "%s/cmd", task_path);
    if (mkdir(cmd_path, 0755) < 0) {
        free(task_ids);
        free(task_path);
        free(file_path);
        free(cmd_path);
        write_u16(fd_reply, ANSTYPE_ER);
        write_u16(fd_reply, ERRCODE_NR);
        return;
    }

    // créer le fichier type dans cmd
    snprintf(file_path, len + 20, "%s/type", cmd_path);
    fd = open(file_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    write_u16(fd, type);
    close(fd);

    int error = 0;
    // pour chaque tâche source
    for (u32 i = 0; i < num_tasks; i++) {
        // allouer le chemin source
        char *src_cmd_path = allouer_chemin(len + 20);
        if (!src_cmd_path) {
            error = 1;
            break;
        }
        snprintf(src_cmd_path, len + 20, "%s/%lu/cmd", tasks_dir, task_ids[i]);
        
        // lire la commande source
        command_t *cmd = read_command(src_cmd_path);
        free(src_cmd_path);

        // écrire la sous-commande dans le répertoire cmd de la nouvelle tâche
        if (cmd) {
            char *dest_sub_path = allouer_chemin(len + 20);
            if (!dest_sub_path) {
                free_command(cmd);
                free(cmd);
                error = 1;
                break;
            }
            
            snprintf(dest_sub_path, len + 20, "%s/%u", cmd_path, i);
            if (creer_commande_arbo(dest_sub_path, cmd) < 0) error = 1;
            free(dest_sub_path);
            free_command(cmd);
            free(cmd);
        }
    }

    free(task_path);
    free(file_path);
    free(cmd_path);

    if (error) {
        free(task_ids);
        write_u16(fd_reply, ANSTYPE_ER);
        write_u16(fd_reply, ERRCODE_NR);
    } else {
        // suppression des tâches d'origine si succès
        for (u32 i = 0; i < num_tasks; i++) {
            size_t p_len = strlen(tasks_dir) + 30;
            char *p = allouer_chemin(p_len);
            if (p) {
                snprintf(p, p_len, "%s/%lu", tasks_dir, task_ids[i]);
                supprimer_repertoire_recursif(p);
                free(p);
            }
        }
        free(task_ids);

        write_u16(fd_reply, ANSTYPE_OK);
        write_u64(fd_reply, new_id);
    }
}