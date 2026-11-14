#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/stat.h>
#include <signal.h>
#include "operationsys.h"
#include "parsing.h"
#include "memoire.h"
#include "executeur.h"
#include "reseau.h"
#include "communication.h"
#include "affichage.h"
#include <sys/wait.h>

void gestionnaire_sigchild(int sig){
    (void)sig;
    int status;
    while(waitpid(-1, &status, WNOHANG) > 0){
        //ca nettoie les zomblards
    }
}

void gerer_requete(int fd_req, const char *pipes_dir, const char *tasks_dir){
    size_t len = strlen(pipes_dir) + 30;
    char *reply_pipe = allouer_chemin(len);
    
    if (!reply_pipe) {
        return;
    }

    snprintf(reply_pipe, len, "%s/erraid-reply-pipe", pipes_dir);

    u16 opcode = read_u16(fd_req);

    if (opcode == 0) {
        free(reply_pipe);
        return;
    }

    int fd_rep = open(reply_pipe, O_WRONLY);
    if (fd_rep < 0) {
        perror("Erreur ouverture reply pipe");
        free(reply_pipe);
        return;
    }

    switch(opcode){
        case OPCODE_LIST:
            printf("ERRAID TRAITE LISTE \n");
            traiter_list(tasks_dir, fd_rep);
            break;

        case OPCODE_TX:
            u64 taskid_tx = read_u64(fd_req);
            traiter_times_exitcodes(tasks_dir, taskid_tx, fd_rep);
            break;
            
        case OPCODE_STDOUT:
            u64 taskid_so = read_u64(fd_req);
            traiter_stdout(tasks_dir, taskid_so, fd_rep);
            break;
            
        case OPCODE_STDERR:
            u64 taskid_se = read_u64(fd_req);
            traiter_stderr(tasks_dir, taskid_se, fd_rep);
            break;
            
        case OPCODE_TERMINATE:
            write_u16(fd_rep, ANSTYPE_OK);
            close(fd_rep);
            free(reply_pipe);
            printf("TERMINATE");
            exit(0);
            break;
        
        case OPCODE_REMOVE:
            u64 taskid_rm = read_u64(fd_req);
            traiter_remove(tasks_dir, taskid_rm, fd_rep);
            break;

        case OPCODE_CREATE:
            traiter_create(tasks_dir, fd_req, fd_rep);
            break;

        case OPCODE_COMBINE:
            traiter_combine(tasks_dir, fd_req, fd_rep);
            break;
            
        default:
        //autre opcode on fait juste erreur
            write_u16(fd_rep, ANSTYPE_ER);
            write_u16(fd_rep, ERRCODE_NR);
            break;
    }

    close(fd_rep);
    
    free(reply_pipe);

}

// boucle principale du démon
void lancer_demon(const char *tasks_dir, const char *pipes_dir) {

    //sidaction
    struct sigaction sa;
    memset(&sa, 0, sizeof(struct sigaction));
    sa.sa_handler = gestionnaire_sigchild;
    sa.sa_flags = SA_NOCLDSTOP | SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("erreur sigaction SIGCHLD");
        return;
    }

    size_t len = strlen(pipes_dir) + 30;
    char *request_pipe = allouer_chemin(len);
    if (!request_pipe) {
        return;
    }
    snprintf(request_pipe, len, "%s/erraid-request-pipe", pipes_dir);
    
    int fd_req_permanent = open(request_pipe, O_RDONLY | O_NONBLOCK);
    if (fd_req_permanent < 0) {
        perror("Erreur ouverture permanente du request pipe");
        free(request_pipe);
        return;
    }
    free(request_pipe);
    
    while (1) {
        // on récupère l'heure actuelle
        time_t maintenant = time(NULL);
        struct tm *temps_maintenant = localtime(&maintenant);
        int sec = temps_maintenant->tm_sec;
        
        // on synchronise sur la minute pile
        if (sec != 0) {
            for (int i = 0; i < (60 - sec); i++) {
                gerer_requete(fd_req_permanent, pipes_dir, tasks_dir);
                sleep(1);
            }
            maintenant = time(NULL);
            temps_maintenant = localtime(&maintenant);
        }
        
        // on parcourt toutes les tâches dans tasks_dir
        DIR *dir = opendir(tasks_dir);
        if (dir) {
            struct dirent *entree;
            while ((entree = readdir(dir)) != NULL) {
                // on ignore . et .. (inutile / pas de taches)
                if (strcmp(entree->d_name, ".") == 0 || 
                    strcmp(entree->d_name, "..") == 0)
                    continue;
                
                // on construit le chemin complet
                size_t len = strlen(tasks_dir) + strlen(entree->d_name) + 2;
                char *chemin_tache = allouer_chemin(len);
                if (!chemin_tache) {
                    continue;
                }
                snprintf(chemin_tache, len, "%s/%s", tasks_dir, entree->d_name);
                
                // on vérifie que c'est un répertoire avec un nom numérique
                struct stat st;
                if (stat(chemin_tache, &st) == 0 && 
                    S_ISDIR(st.st_mode) && 
                    est_numerique(entree->d_name)) {

                    // maintenant -> on fait un fork pour les tâches simultanés
                    pid_t fils = fork();
                    if (fils == 0) {
                        // si c'est le fils alors on exécute la tâche et on quitte
                        verifier_tache(tasks_dir, entree->d_name, temps_maintenant);
                        exit(0);
                    } else if (fils < 0) {
                        perror("Erreur fork");
                    }
                    // si c'est le père alors on continue la boucle SANS attendre
                }
                free(chemin_tache);
            }
            closedir(dir);
        } else {
            perror("Erreur ouverture répertoire tasks");
        }
        
        // on attends 1 minute à chaque fois 
        for (int i = 0; i < (60 - sec); i++) {
            gerer_requete(fd_req_permanent, pipes_dir, tasks_dir);
            sleep(1);
        }
    }
    close(fd_req_permanent);
}


int main(int argc, char *argv[]) {

    char *tasks_dir = NULL;
    char *pipe_dir = NULL;
    char *racine_dir = NULL;
    char *request_pipe = NULL;
    char *reply_pipe = NULL;
    int mode_demon = 1;

    int opt;

    while ((opt = getopt(argc, argv, "R:P:F")) != -1) {
        switch (opt) {
            case 'R': 
                racine_dir = optarg;
                break;
            case 'P': 
                pipe_dir = optarg;
                break;
            case 'F':
                mode_demon = 0;
                break;
            default:
                afficher_usage(argv[0]);
        }
    }

    // déterminer le chemin du répertoire des tâches
    if (racine_dir == NULL) {

        // si pas d'arguments : on utilises le chemin par défaut /tmp/$USER/erraid/
        const char *utilisateur = getenv("USER");
        if (!utilisateur) {
            utilisateur = "default";
        }
        size_t len = strlen("/tmp/") + strlen(utilisateur) + strlen("/erraid") + 1;
        racine_dir = allouer_chemin(len);
        if (!racine_dir) {
            return EXIT_FAILURE;
        }
        snprintf(racine_dir, len, "/tmp/%s/erraid", utilisateur);
        
        // on crée le répertoire s'il n'existe pas
        if (creer_repertoire(racine_dir) != 0) {
            free(racine_dir);
            return EXIT_FAILURE;
        }
    }
    else {
        struct stat st;
        if (stat(racine_dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
            fprintf(stderr, "Erreur: Le répertoire %s n'existe pas ou n'est pas accessible\n", racine_dir);
            return EXIT_FAILURE;
        }
    }

        //on construit tasks/
    size_t tasks_len = strlen(racine_dir) + strlen("/tasks") + 1;
    tasks_dir = allouer_chemin(tasks_len);
    if (!tasks_dir) {
        free(racine_dir);
        return EXIT_FAILURE;
    }
    snprintf(tasks_dir, tasks_len, "%s/tasks", racine_dir);

        // oncrée tasks s'il n'existe pas
    if (creer_repertoire(tasks_dir) != 0) {
        free(racine_dir);
        free(tasks_dir);
        return EXIT_FAILURE;
    }

    if (pipe_dir == NULL) {
        // défaut: <racine>/pipes
        size_t len = strlen(racine_dir) + strlen("/pipes") + 1;
        pipe_dir = allouer_chemin(len);
        if (!pipe_dir) {
            free(racine_dir);
            free(tasks_dir);
            return EXIT_FAILURE;
        }
        snprintf(pipe_dir, len, "%s/pipes", racine_dir);
    }

    //on crée les tubes
    if (creer_tubes(pipe_dir, &request_pipe, &reply_pipe) != 0) {
        fprintf(stderr, "Erreur lors de la création des tubes nommés\n");
        free(racine_dir);
        free(tasks_dir);
        free(pipe_dir);
        return EXIT_FAILURE;
    }

    if (mode_demon) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("Erreur fork demon");
            return EXIT_FAILURE;
        }
        if (pid > 0) {
            exit(EXIT_SUCCESS);
        }

        if (setsid() < 0) {
            perror("Erreur setsid");
            return EXIT_FAILURE;
        }
        
        pid = fork();
        if (pid < 0) {
            perror("Erreur 2eme fork demon");
            exit(EXIT_FAILURE);
        }
        if (pid > 0) {
            exit(EXIT_SUCCESS);
        }
    }

    lancer_demon(tasks_dir, pipe_dir);

    return EXIT_SUCCESS;

}