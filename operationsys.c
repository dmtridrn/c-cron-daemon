#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include "operationsys.h"
#include "memoire.h"
#include <linux/limits.h>  
#include <dirent.h>

// alloue un buffer dynamique de taille suffisante pour un chemin
char* allouer_chemin(size_t taille_base) {
    char *buffer = malloc(taille_base);
    if (!buffer) {
        perror("malloc chemin");
        return NULL;
    }
    return buffer;
}

// crée un fichier vide (ou tronque s'il existe)
char * creer_fichier_vide(char *chemin, char *nom_fichier) {
    size_t len = strlen(chemin) + strlen(nom_fichier) + 2;
    char *chemin_entier = allouer_chemin(len);
    if (!chemin_entier) {
        return NULL;
    }
    snprintf(chemin_entier, len, "%s/%s", chemin, nom_fichier);
    int fd = open(chemin_entier, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) {
        perror("Erreur création fichier de sortie");
        return NULL;
    }
    close(fd);
    return chemin;
}

// écrit du contenu dans un fichier (écrase le contenu existant)
void ecrire_contenu_dans_fichier(const char *chemin, const char *contenu) {
    int fd = open(chemin, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) {
        perror("Erreur création fichier de sortie");
        return;
    }
    write(fd, contenu, strlen(contenu));
    write(fd, "\n", 1);
    close(fd);
}

// écrit le timestamp et l'exitcode dans times-exitcodes
void ecrire_time_exitcode(const char *racine_tache, u16 exitcode) {
    size_t len = strlen(racine_tache) + 30;
    char *chemin = allouer_chemin(len);
    if (!chemin) return;
    
    snprintf(chemin, len, "%s/times-exitcodes", racine_tache);
    
    int fd = open(chemin, O_CREAT | O_WRONLY | O_APPEND, 0644);
    if (fd < 0) {
        perror("Erreur ouverture times-exitcodes");
        return;
    }
    
    // timestamp UTC (secondes depuis epoch)
    i64 timestamp = (i64)time(NULL);
    
    // conversion en big-endian pour l'écriture
    i64 timestamp_be = htobe64(timestamp);
    u16 exitcode_be = htons(exitcode);
    
    write(fd, &timestamp_be, sizeof(i64));
    write(fd, &exitcode_be, sizeof(u16));
    
    close(fd);
    free(chemin);
}

// crée un répertoire et son arborescence
int creer_repertoire(const char *path) {
    struct stat st;
    
    // vérifier si le répertoire existe déjà
    if (stat(path, &st) == 0) {
        if (S_ISDIR(st.st_mode)) return 0;
        else {
            fprintf(stderr, "Erreur: %s existe mais n'est pas un répertoire\n", path);
            return -1;
        }
    }
    
    pid_t pid = fork();
    if (pid < 0) {
        perror("Erreur fork pour mkdir");
        return -1;
    }
    if (pid == 0) { // processus fils
        execlp("mkdir", "mkdir", "-p", path, NULL);
        // si execvp échoue
        perror("Erreur execvp mkdir");
        exit(EXIT_FAILURE);
    }
    waitpid(pid, NULL, 0);
    
    return 0;
}

//cree tubes pour communication
int creer_tubes(const char *pipes_dir, char **request_pipe, char **reply_pipe){
    if (creer_repertoire(pipes_dir) != 0) return -1;

    size_t pipe_len = strlen(pipes_dir) + 30; //marge au cas ou
    *request_pipe = allouer_chemin(pipe_len);
    *reply_pipe = allouer_chemin(pipe_len);
    
    if (!*request_pipe || !*reply_pipe) {
        free(*request_pipe);
        free(*reply_pipe);
        return -1;
    }

    snprintf(*request_pipe, pipe_len, "%s/erraid-request-pipe", pipes_dir);
    snprintf(*reply_pipe, pipe_len, "%s/erraid-reply-pipe", pipes_dir);

    //s'ils existent deja on supprime les anciens
    unlink(*request_pipe);
    unlink(*reply_pipe);
    
    if (mkfifo(*request_pipe, 0666) != 0 || mkfifo(*reply_pipe, 0666) != 0) {
        perror("erreur creation pipe(s)");
        free(*request_pipe);
        free(*reply_pipe);
        return -1;
    }
    
    return 0;
}

// supprime un répertoire et tout son contenu de manière récursive
int supprimer_repertoire_recursif(const char *path) {
    DIR *dir = opendir(path);
    if (!dir) return -1;
    
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        
        //chemin complet
        size_t len = strlen(path) + strlen(entry->d_name) + 2;
        char *chemin_complet = allouer_chemin(len);
        if (!chemin_complet) {
            closedir(dir);
            return -1;
        }
        snprintf(chemin_complet, len, "%s/%s", path, entry->d_name);
        
        struct stat st;
        if (stat(chemin_complet, &st) == 0) {
            if (S_ISDIR(st.st_mode)) supprimer_repertoire_recursif(chemin_complet);
            else unlink(chemin_complet);
        }
        
        // Libérer chemin_complet
        free(chemin_complet);
    }
    
    closedir(dir);
    
    // Maintenant le répertoire est vide, on peut le supprimer
    return rmdir(path);
}