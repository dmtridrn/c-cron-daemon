#include "parsing.h"
#include <unistd.h>
#include <endian.h>   // pour be16toh, be32toh, be64toh
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>  
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h> 
#include <linux/limits.h>
#include <errno.h>
#include "operationsys.h"

// lit un u16 depuis un file descriptor
u16 read_u16(int fd) {
    u16 value;
    
    ssize_t bytes_read = read(fd, &value, sizeof(u16)); //2 octets

    if (bytes_read < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        perror("Erreur lecture u16");
        return 0;
    }
    if (bytes_read == 0) return 0; //pour pas spam
    
    if (bytes_read != sizeof(u16)) {
        perror("Erreur lecture u16");
        return 0;
    }
    
    return be16toh(value);
}

// lit un u32 depuis un file descriptor
u32 read_u32(int fd) {
    u32 value;
    
    ssize_t bytes_read = read(fd, &value, sizeof(u32)); // 4 octets
    
    if (bytes_read != sizeof(u32)) {
        perror("Erreur lecture u32");
        return 0;
    }
    
    return be32toh(value);
}

// lit un u64 depuis un file descriptor
u64 read_u64(int fd) {
    u64 value;
    
    ssize_t bytes_read = read(fd, &value, sizeof(u64)); // 8 octets
    
    if (bytes_read != sizeof(u64)) {
        perror("Erreur lecture u64");
        return 0;
    }
    
    return be64toh(value);
}

// lit un i64 depuis un file descriptor
i64 read_i64(int fd) {
    i64 value;
    
    ssize_t bytes_read = read(fd, &value, sizeof(i64));//8
    
    if (bytes_read != sizeof(i64)) {
        perror("Erreur lecture i64");
        return 0;
    }
    
    return be64toh(value);
}

// fonction pour convertir le contenu d'un fichier en string_t
string_t read_string(int fd){
    string_t str;

    str.length = read_u32(fd);

    // malloc +1 pour le \0
    str.data = malloc(str.length + 1);
    if(str.data == NULL){
        // si erreur d'allocation, on remplit str avec une chaîne vide
        perror("Erreur malloc string_t");
        str.length = 0;
        str.data = malloc(1);
        if (str.data) str.data[0] = '\0';
        return str;
    }

    // lecture
    size_t total_lus = 0;
    while(total_lus < str.length) {
        ssize_t lus = read(fd, str.data + total_lus, str.length - total_lus);
        if (lus < 0) {
            if (errno == EINTR) continue;
            perror("Erreur lecture string data");
            free(str.data);
            str.length = 0;
            str.data = malloc(1);
            if (str.data) str.data[0] = '\0';
            return str;
        }
        if (lus == 0) break; // EOF
        total_lus += lus;
    }

    if (total_lus != str.length) {
        // si erreur de lecture, on remplit str avec une chaîne vide
        free(str.data);
        str.length = 0;
        str.data = malloc(1);
        if (str.data) str.data[0] = '\0';
        return str;
    }
    str.data[str.length] = '\0';

    return str;
}

// lit un timing
timing_t read_timing(int fd){
    timing_t timing;

    //définies plus haut
    timing.minutes = read_u64(fd);
    timing.heures = read_u32(fd);

    //lecture directe pour les jours
    ssize_t lus = read(fd, &timing.jours, sizeof(u8));
    if (lus != sizeof(u8)) perror("Erreur lecture daysofweek");
    return timing;
}

// vérifie si une chaîne est numérique
int est_numerique(const char *nom) {
    if (!nom || !nom[0]) return 0;
    for (const char *p = nom; *p; p++) {
        if (*p < '0' || *p > '9') return 0;  // pas un chiffre
    }
    return 1;
}

// lit une commande depuis l'arborescence
command_t *read_command(const char *cmd_path){
    command_t *cmd = malloc(sizeof(command_t));

    if(!cmd){
        perror("malloc commande");
        return NULL;
    }
    memset(cmd, 0, sizeof(command_t));
    
    size_t len = strlen(cmd_path) + 20;
    char *chemin = allouer_chemin(len);
    if (!chemin) {
        free(cmd);
        return NULL;
    }

    //on lit type
    snprintf(chemin, len, "%s/type", cmd_path);
    int fd = open(chemin, O_RDONLY);
    if(fd < 0){
        perror("open du type");
        free(chemin);
        free(cmd);
        return NULL;
    }
    cmd->type = read_u16(fd);
    close(fd);

    //CAS COMMANDE SIMPLE
    if (cmd->type == CMD_TYPE_SIMPLE) {
        snprintf(chemin, len, "%s/argv", cmd_path);
        fd = open(chemin, O_RDONLY);
        if (fd < 0) {
            perror("open argv");
            free(chemin);
            free(cmd);
            return NULL;
        }
        
        cmd->args.argc = read_u32(fd);
        
        cmd->args.argv = malloc(cmd->args.argc * sizeof(string_t));
        if (!cmd->args.argv) {
            perror("malloc argv");
            close(fd);
            free(chemin);
            free(cmd);
            return NULL;
        }
        
        for (u32 i = 0; i < cmd->args.argc; i++) {
            cmd->args.argv[i] = read_string(fd);
        }
        close(fd);
        
        cmd->nsub = 0;
        cmd->subcmds = NULL;
        
        free(chemin);
        return cmd;
    }

    //CAS SOUS-COMMANDES
    DIR *dir = opendir(cmd_path);
    if (!dir) {
        perror("opendir cmd");
        free(chemin);
        free(cmd);
        return NULL;
    }

    u32 count = 0;
    struct dirent *entry;
    struct stat st;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        
        snprintf(chemin, len, "%s/%s", cmd_path, entry->d_name);
        if (stat(chemin, &st) == 0 && S_ISDIR(st.st_mode)) {
            if (est_numerique(entry->d_name)) {
                count++;
            }
        }
    }
    closedir(dir);

    cmd->nsub = count;

    if (count == 0) { 
        cmd->subcmds = NULL;
        cmd->args.argc = 0;
        cmd->args.argv = NULL;
        free(chemin);
        return cmd;
    }

    cmd->subcmds = malloc(count * sizeof(command_t));
    if (!cmd->subcmds) {
        perror("malloc subcmds");
        free(chemin);
        free(cmd);
        return NULL;
    }

    for (u32 i = 0; i < count; i++) {
        snprintf(chemin, len, "%s/%u", cmd_path, i);
        command_t *subcmd = read_command(chemin);  // appel récursif pour chaque sous commande
        if (subcmd) {
            cmd->subcmds[i] = *subcmd;
            free(subcmd);
        } else {
            // initialiser une commande vide en cas d'erreur
            memset(&cmd->subcmds[i], 0, sizeof(command_t));
        }
    }
        
    // pas d'args pour les commandes composites
    cmd->args.argc = 0;
    cmd->args.argv = NULL;
    
    free(chemin);
    return cmd;
}

// lit une commande depuis un file descriptor (pour le protocole)
command_t *read_command_from_fd(int fd) {
    command_t *cmd = malloc(sizeof(command_t));
    if (!cmd) {
        perror("malloc commande");
        return NULL;
    }
    memset(cmd, 0, sizeof(command_t));
    
    cmd->type = read_u16(fd);
    
    if (cmd->type == CMD_TYPE_SIMPLE) {
        // lire les arguments
        cmd->args.argc = read_u32(fd);
        cmd->args.argv = malloc(cmd->args.argc * sizeof(string_t));
        if (!cmd->args.argv) {
            perror("malloc argv");
            free(cmd);
            return NULL;
        }
        for (u32 i = 0; i < cmd->args.argc; i++) {
            cmd->args.argv[i] = read_string(fd);
        }
        cmd->nsub = 0;
        cmd->subcmds = NULL;
        return cmd;
    }
    
    // commande composite
    u32 nbcmds = read_u32(fd);
    cmd->nsub = nbcmds;
    cmd->args.argc = 0;
    cmd->args.argv = NULL;
    
    if (nbcmds == 0) {
        cmd->subcmds = NULL;
        return cmd;
    }
    
    cmd->subcmds = malloc(nbcmds * sizeof(command_t));
    if (!cmd->subcmds) {
        perror("malloc subcmds");
        free(cmd);
        return NULL;
    }
    
    for (u32 i = 0; i < nbcmds; i++) {
        command_t *subcmd = read_command_from_fd(fd);
        if (subcmd) {
            cmd->subcmds[i] = *subcmd;
            free(subcmd);
        } else {
            memset(&cmd->subcmds[i], 0, sizeof(command_t));
        }
    }
    
    return cmd;
}

// lit toutes les entrées d'un fichier times-exitcodes depuis le chemin
time_exitcode_t* read_time_exitcodes(const char *filepath, u32 *count) {
    int fd = open(filepath, O_RDONLY);
    if (fd < 0) {
        perror("Erreur ouverture times-exitcodes");
        *count = 0;
        return NULL;
    }
    
    // déterminer la taille du fichier
    off_t file_size = lseek(fd, 0, SEEK_END);
    if (file_size < 0) {
        perror("Erreur lseek");
        close(fd);
        *count = 0;
        return NULL;
    }
    lseek(fd, 0, SEEK_SET);
    
    // chaque entrée fait 10 octets (8 pour i64 + 2 pour u16)
    u32 num_entries = (u32)(file_size / 10);
    *count = num_entries;
    
    if (num_entries == 0) {
        close(fd);
        return NULL;
    }
    
    // allouer le tableau
    time_exitcode_t *entries = malloc(num_entries * sizeof(time_exitcode_t));
    if (entries == NULL) {
        perror("Erreur malloc time_exitcode_t array");
        close(fd);
        *count = 0;
        return NULL;
    }
    
    // lire toutes les entrées
    for (u32 i = 0; i < num_entries; i++) {
        // lire directement (8 octets pour i64 + 2 octets pour u16)
        entries[i].time = read_i64(fd);
        entries[i].exitcode = read_u16(fd);
    }
    
    close(fd);
    return entries;
}


// fonction interne pour lire les bits (minutes/heures/jours)

// les fonctions de parsing sont sous format crontab
// bit 3 = minute/heure/jour 3
// "*" -> tt les minutes/heures/jours 
// "-" -> aucune minute/heure/jour

static u64 read_cron_bits(const char *str, int max_val) {
    u64 bits = 0;

    // les cas spéciaux d'abord

    // on utilise u64 donc c'est safe
    if (!str || strcmp(str, "*") == 0) return (1ULL << (max_val + 1)) - 1; // tt les bits à 1

    if (strcmp(str, "-") == 0) return 0; // aucune valeur

    // parse normalement (ex : "4-9,47")
    char *copie = strdup(str);
    if (!copie) {
        perror("malloc read cron bits");
        return 0;
    }

    char *context = NULL;
    char *segment = strtok_r(copie, ",", &context);
    
    while (segment) {
        // cherche un tiret pour les ranges
        char *sep = strchr(segment, '-');
        if (sep) {
            // si on a 1-5 a.k.a valeurs 1,2,3,4,5
            int debut = atoi(segment);
            int fin = atoi(sep + 1);

            // on vérifie les limites (0 à max_val)
            if (debut < 0) debut = 0;
            if (fin > max_val) fin = max_val;
            
            if (debut > fin) { 
                // range invalide donc on ignore
                segment = strtok_r(NULL, ",", &context);
                continue;
            }

            // activer tous les bits de debut à fin inclus
            for (int i = debut; i <= fin; i++) {
                bits |= (1ULL << i);
            } 
        } else {
            // nb simple -> "3" a.k.a valeur 3
            int num = atoi(segment);
            if (num >= 0 && num <= max_val) {
                bits |= (1ULL << num);
            }
        }
        segment = strtok_r(NULL, ",", &context);
    }
    
    free(copie);
    return bits;
}

u64 read_minutes_string(const char *str) {
    // 0-59
    return read_cron_bits(str, 59);
}

u32 read_heures_string(const char *str) {
    // 0-23 (cast en u32)
    return (u32)read_cron_bits(str, 23);
}

u8 read_jours_string(const char *str) {
    // 0-6 (cast en u8)
    return (u8)read_cron_bits(str, 6);
}