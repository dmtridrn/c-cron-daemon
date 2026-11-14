#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "parsing.h"
#include "planificateur.h"
#include <time.h>
#include "executeur.h"
#include <errno.h>
#include "memoire.h"
#include "operationsys.h"
#include "affichage.h"


u16 executer_commande_simple(arguments_t *args, const char *racine_tache) {
    
    // si racine_tache est NULL ->  on est à l'intérieur d'un pipeline
    if (racine_tache == NULL) {
        if (args->argc == 0) return 0xFFFF;

        char **argv = malloc((args->argc + 1) * sizeof(char *));
        if (!argv) return 0xFFFF;
        for (u32 i = 0; i < args->argc; i++) argv[i] = args->argv[i].data;
        argv[args->argc] = NULL;

        pid_t pid = fork();
        if (pid < 0) { free(argv); return 0xFFFF; }
        
        if (pid == 0) { // fils pipeline
            execvp(argv[0], argv);
            fprintf(stderr, "Erreur execvp pipeline: %s\n", strerror(errno));
            exit(errno);
        }
        
        // parent pipeline
        free(argv);
        int status;
        waitpid(pid, &status, 0);
        return WIFEXITED(status) ? (u16)WEXITSTATUS(status) : 0xFFFF;
    }

    size_t len = strlen(racine_tache) + 20;
    char *std_out = allouer_chemin(len);
    char *std_err = allouer_chemin(len);
    if (!std_out || !std_err) {
        free(std_out);
        free(std_err);
        return 0xFFFF;
    }
    snprintf(std_out, len, "%s/stdout", racine_tache);
    snprintf(std_err, len, "%s/stderr", racine_tache);

    if (args->argc == 0) {
        ecrire_contenu_dans_fichier(std_err, "Erreur: commande simple sans arguments.");
        free(std_out);
        free(std_err);
        return 0xFFFF;
    }

    // convertit arguments_t en char** pour execvp
    char **argv = malloc((args->argc + 1) * sizeof(char *));
    if (argv == NULL) {
        perror("Erreur malloc argv pour exec");
        free(std_out);
        free(std_err);
        return 0xFFFF;
    }
    for (u32 i = 0; i < args->argc; i++) {
        argv[i] = args->argv[i].data;
    }
    argv[args->argc] = NULL;

    pid_t pid = fork();
    if (pid < 0) {
        perror("Erreur fork");
        free(argv);
        free(std_out);
        free(std_err);
        return 0xFFFF;
    }
    
    if (pid == 0) { // fils
        int fd_stdout = open(std_out, O_CREAT | O_WRONLY | O_APPEND, 0644); 
        if (fd_stdout >= 0){
            dup2(fd_stdout, STDOUT_FILENO);
            close(fd_stdout); // on ferme le descripteur
        }

        int fd_stderr = open(std_err, O_CREAT | O_WRONLY | O_APPEND, 0644);
        if (fd_stderr >= 0){
            dup2(fd_stderr, STDERR_FILENO);
            close(fd_stderr); // pareil
        } 

        // on ferme pas ici 
        
        execvp(argv[0], argv);
        
        // si execvp retourne une erreur
        fprintf(stderr, "Erreur execvp pour '%s': %s\n", argv[0], strerror(errno));
        exit(errno);
    }

    // parent
    int status;
    waitpid(pid, &status, 0);
    free(argv);
    
    // extraction du code de sortie
    u16 exitcode = 0xFFFF; // valeur par défaut en cas d'interruption
    if (WIFEXITED(status)) {
        exitcode = (u16)WEXITSTATUS(status);
    }
    
    free(std_out);
    free(std_err);
    return exitcode;
}



u16 executer_commande(command_t *cmd, const char *racine_tache) {
    if (!cmd) return 0xFFFF;

    u16 exitcode = 0xFFFF;
    
    if (cmd->type == CMD_TYPE_SIMPLE) {
        exitcode = executer_commande_simple(&cmd->args, racine_tache);
    } 
    else if (cmd->type == TYPE_SQ) {
        for (u32 i = 0; i < cmd->nsub; i++) {
            exitcode = executer_commande(&cmd->subcmds[i], racine_tache);
        }
    } 
    else if (cmd->type == TYPE_PL) {
        exitcode = executer_pipeline(cmd, racine_tache);
    } 
    else if (cmd->type == TYPE_IF) { 
        exitcode = executer_conditionnelle(cmd, racine_tache);
    }
    else {
        fprintf(stderr, "!!! Erreur: type de commande inconnu (0x%04X)\n", cmd->type);
        fflush(stderr);
    }
    
    return exitcode;
}

u16 executer_pipeline(command_t *cmd, const char *racine_tache) {
    if (!cmd || cmd->nsub < 2) return 0xFFFF;
    
    pid_t *pids = malloc(cmd->nsub * sizeof(pid_t));
    if (!pids) return 0xFFFF;

    fflush(stdout);
    
    // préparation des chemins logs si racine_tache existe
    char *path_out = NULL;
    char *path_err = NULL;

    if (racine_tache) {
        size_t len = strlen(racine_tache) + 20;
        path_out = allouer_chemin(len);
        path_err = allouer_chemin(len);
        if (!path_out || !path_err) {
            free(path_out);
            free(path_err);
            free(pids);
            return 0xFFFF;
        }
        snprintf(path_out, len, "%s/stdout", racine_tache);
        snprintf(path_err, len, "%s/stderr", racine_tache);
    }
    
    int prev_pipe = -1;
    int fds[2];

    for (u32 i = 0; i < cmd->nsub; i++) {
        
        // création pipe (sauf dernier)
        if (i < cmd->nsub - 1) {
            if (pipe(fds) < 0) { 
                perror("pipe"); 
                if (prev_pipe != -1) close(prev_pipe);
                free(pids);
                free(path_out);
                free(path_err);
                return 0xFFFF;
            }
        }

        pid_t pid = fork();
        if (pid < 0) {
            perror("fork"); 
            if (prev_pipe != -1) close(prev_pipe);
            if (i < cmd->nsub - 1) { close(fds[0]); close(fds[1]); }
            free(pids);
            free(path_out);
            free(path_err);
            return 0xFFFF;
        }

        if (pid == 0) { // enfant

            // redirection entrée (depuis tube précédent)
            if (prev_pipe != -1) {
                dup2(prev_pipe, STDIN_FILENO);
                close(prev_pipe);
            }

            // redirection sortie (vers tube suivant/fichier stdout si dernier)
            if (i < cmd->nsub - 1) {
                dup2(fds[1], STDOUT_FILENO);
                close(fds[0]); close(fds[1]);
            } else {
                // Dsi dernier élement, on écrit dans le fichier stdout SI il existe
                if (path_out) {
                    int fd_out = open(path_out, O_CREAT | O_WRONLY | O_APPEND, 0644);
                    if (fd_out >= 0) { dup2(fd_out, STDOUT_FILENO); close(fd_out); }
                }
            }

            // redirection erreur SI le fichier existe
            if (path_err) {
                int fd_err = open(path_err, O_CREAT | O_WRONLY | O_APPEND, 0644);
                if (fd_err >= 0) { dup2(fd_err, STDERR_FILENO); close(fd_err); }
            }

            // on execute pour pipeline (NULL -> pas de pb de redirections)
            free(pids);
            free(path_out);
            free(path_err);
            u16 res = executer_commande(&cmd->subcmds[i], NULL);
            exit(res);
        }

        // le père
        pids[i] = pid;
        if (prev_pipe != -1) close(prev_pipe); // Fermer lecture précédente
        if (i < cmd->nsub - 1) {
            prev_pipe = fds[0]; // Garder lecture actuelle pour le suivant
            close(fds[1]);      // Fermer écriture
        }
        
    }
    if (prev_pipe != -1) close(prev_pipe);
    if (path_out) free(path_out);
    if (path_err) free(path_err);

    // Attente des enfants
    u16 exitcode = 0;
    for (u32 i = 0; i < cmd->nsub; i++) {
        int status;
        waitpid(pids[i], &status, 0); 
        
        if (i == cmd->nsub - 1) {
            if (WIFEXITED(status)) exitcode = (u16)WEXITSTATUS(status);
            else exitcode = 0xFFFF;
        }
    }
    free(pids);
    return exitcode;
}

u16 executer_conditionnelle(command_t *cmd, const char *racine_tache) {
    if (!cmd || cmd->nsub < 2) return 0xFFFF; // il faut au moins if et then

    u16 res_condition = executer_commande(&cmd->subcmds[0], racine_tache);
    u16 final_res = 0;

    if (res_condition == 0) {
        // si ça fonctionne, on execute then a.k.a cmd[1]
        final_res = executer_commande(&cmd->subcmds[1], racine_tache);
    } 
    else {
        // si on échoue, on exécuter else a.k.a (cmd[2]) s'il existe
        if (cmd->nsub >= 3) {
            final_res = executer_commande(&cmd->subcmds[2], racine_tache);
        } else {
            // pas de branche else, on retourne 0 
            final_res = 0; 
        }
    }

    return final_res;
}


void verifier_tache(const char *tasks_dir, const char *id_tache, struct tm *maintenant) {
    size_t len = strlen(tasks_dir) + strlen(id_tache) + 30;
    char *chemin = allouer_chemin(len);
    if (!chemin) {
        return;
    }
    
    // on lit le fichier timing D'ABORD
    snprintf(chemin, len, "%s/%s/timing", tasks_dir, id_tache);
    int fd = open(chemin, O_RDONLY);
    if (fd < 0) {
        return;
    }
    
    timing_t timing = read_timing(fd);
    close(fd);
    
    // on vérifie si la tache doit s'exécuter maintenant
    if (!devrait_sexec(&timing, maintenant)) {
        free(chemin);
        return;
    }
    
    // on vérifie si la tâche a déjà été exécutée cette minute
    snprintf(chemin, len, "%s/%s/times-exitcodes", tasks_dir, id_tache);
    u32 count = 0;
    time_exitcode_t *entries = read_time_exitcodes(chemin, &count);

        if (entries && count > 0) {
            time_exitcode_t derniere = entries[count - 1];
            time_t now_minutes = (time(NULL) / 60) * 60;
            time_t last_minutes = (derniere.time / 60) * 60;
            
            if (now_minutes == last_minutes) {
                free(entries);
                free(chemin);
                return;
            }

        free(entries);
    }
    
    // construction du chemin vers la racine de la tâche
    size_t racine_len = strlen(tasks_dir) + strlen(id_tache) + 2;
    char *racine_tache = allouer_chemin(racine_len);
    if (!racine_tache) {
        free(chemin);
        return;
    }
    snprintf(racine_tache, racine_len, "%s/%s", tasks_dir, id_tache);
    
    // création des fichiers stdout et stderr vides à la racine
    creer_fichier_vide(racine_tache, "stdout");
    creer_fichier_vide(racine_tache, "stderr");
    
    // construction du chemin vers la commande
    snprintf(chemin, len, "%s/%s/cmd", tasks_dir, id_tache);

    command_t *cmd = read_command(chemin);
    free(chemin);
    u16 exitcode = 0xFFFF;
    if(cmd){
        exitcode = executer_commande(cmd, racine_tache);
        free_command(cmd);
        free(cmd);
    }
    
    // écriture du log avec timestamp et exitcode
    ecrire_time_exitcode(racine_tache, exitcode);  
    
    free(racine_tache);
}