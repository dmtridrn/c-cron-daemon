#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <stdbool.h>
#include "parsing.h"
#include "communication.h"
#include "types.h"        
#include "memoire.h"    
#include "operationsys.h"
#include "affichage.h"
#include "executeur.h"

// configure les chemins des pipes de communication
static int setup_pipes(const char *pipes_dir, char **req_path, char **rep_path) {
    size_t len = strlen(pipes_dir) + 30;
    *req_path = allouer_chemin(len);
    *rep_path = allouer_chemin(len);
    
    if (!*req_path || !*rep_path) {
        fprintf(stderr, "malloc\n");
        free(*req_path);
        free(*rep_path);
        return -1;
    }
    
    snprintf(*req_path, len, "%s/erraid-request-pipe", pipes_dir);
    snprintf(*rep_path, len, "%s/erraid-reply-pipe", pipes_dir);
    return 0;
}

// ouvre le pipe de requête et envoie l'opcode
static int open_envoi_opcode(const char *req_path, u16 opcode) {
    int fd = open(req_path, O_WRONLY);
    if (fd < 0) {
        perror("ouverture request pipe");
        return -1;
    }
    write_u16(fd, opcode);
    return fd;
}

// ouvre le pipe de réponse et lit l'anstype
static int open_recevoir_anstype(const char *rep_path, u16 *anstype) {
    int fd = open(rep_path, O_RDONLY);
    if (fd < 0) {
        perror("ouverture reply pipe");
        return -1;
    }
    *anstype = read_u16(fd);
    return fd;
}

// fermeture et libération des ressources des pipes
static void fermer_pipes(char *req_path, char *rep_path, int fd_rep) {
    if (fd_rep >= 0) close(fd_rep);
    free(req_path);
    free(rep_path);
}

// envoie une requête et reçoit l'anstype
static u16 envoyer_requete(const char *pipes_dir, u16 opcode, u64 *taskid, char **req_path, char **rep_path, int *fd_rep) {
    *req_path = NULL; *rep_path = NULL; *fd_rep = -1;
    if (setup_pipes(pipes_dir, req_path, rep_path) < 0) return 0;

    int fd_req = open_envoi_opcode(*req_path, opcode);
    if (fd_req < 0) { fermer_pipes(*req_path, *rep_path, -1); return 0; }
    
    if (taskid) {write_u64(fd_req, *taskid);}
    close(fd_req);

    u16 anstype;
    *fd_rep = open_recevoir_anstype(*rep_path, &anstype);
    if (*fd_rep < 0) { fermer_pipes(*req_path, *rep_path, -1); return 0; }
    
    return anstype;
}

// envoie et reçoit la réponse pour LIST
static void envoyer_list(const char *pipes_dir) {
    char *req_path, *rep_path;
    int fd_rep;
    u16 anstype = envoyer_requete(pipes_dir, OPCODE_LIST, NULL, &req_path, &rep_path, &fd_rep);
    
    if (anstype == 0) return;

    if (anstype == ANSTYPE_OK) {
        // lire le nombre de tâches
        u32 nb_tasks = read_u32(fd_rep);
        
        // pour chaque tâche lire et afficher les infos
        for (u32 i = 0; i < nb_tasks; i++) {
            u64 taskid = read_u64(fd_rep);
            timing_t timing = read_timing(fd_rep);
            command_t *cmd = read_command_from_fd(fd_rep);
            
            string_t timing_str = timing_to_crontab_string(&timing);
            string_t cmd_str = command_to_string(cmd);
            
            printf("%lu: %s %s\n", taskid, timing_str.data, cmd_str.data);
            
            free_string(&timing_str);
            free_string(&cmd_str);
            if (cmd) {
                free_command(cmd);
                free(cmd);
            }
        }
        
    } else if (anstype == ANSTYPE_ER) {
        fermer_pipes(req_path, rep_path, fd_rep);
        exit(1);
    }

    fermer_pipes(req_path, rep_path, fd_rep);
}

// envoie et reçoit la réponse pour TIMES_EXITCODES
static void envoyer_times_exitcodes(const char *pipes_dir, u64 taskid) {
    char *req_path, *rep_path;
    int fd_rep;
    u16 anstype = envoyer_requete(pipes_dir, OPCODE_TX, &taskid, &req_path, &rep_path, &fd_rep);
    
    if (anstype == 0) return;

    if (anstype == ANSTYPE_OK) {
        // lire le nombre d'entrées
        u32 count = read_u32(fd_rep);
        
        // lire et afficher chaque entrée
        for (u32 i = 0; i < count; i++) {
            i64 time = read_i64(fd_rep);
            u16 exitcode = read_u16(fd_rep);
            
            time_t t = (time_t)time;
            struct tm *tm_info = localtime(&t);
            if (tm_info) {
                printf("%04d-%02d-%02d %02d:%02d:%02d %u\n",
                       tm_info->tm_year + 1900,
                       tm_info->tm_mon + 1,
                       tm_info->tm_mday,
                       tm_info->tm_hour,
                       tm_info->tm_min,
                       tm_info->tm_sec,
                       exitcode);
            }
        }
        
    } else if (anstype == ANSTYPE_ER) {
        fermer_pipes(req_path, rep_path, fd_rep);
        exit(1);
    }
    
    fermer_pipes(req_path, rep_path, fd_rep);
}

// envoie et reçoit la réponse pour OUTPUT
static void envoyer_output(const char *pipes_dir, u64 taskid, u16 opcode) {
    char *req_path, *rep_path;
    int fd_rep;
    u16 anstype = envoyer_requete(pipes_dir, opcode, &taskid, &req_path, &rep_path, &fd_rep);
    
    if (anstype == 0) return;

    if (anstype == ANSTYPE_OK) {
        string_t output = read_string(fd_rep);
        if (output.length > 0) {
            if (write(STDOUT_FILENO, output.data, output.length) != output.length) perror("Erreur : échec écriture stdout");
        }
        free_string(&output);
    } else if (anstype == ANSTYPE_ER) {
        fermer_pipes(req_path, rep_path, fd_rep);
        exit(1);
    }
    
    fermer_pipes(req_path, rep_path, fd_rep);
}

// envoie et reçoit la réponse pour TERMINATE
static void envoyer_terminate(const char *pipes_dir){
    char *req_path, *rep_path;
    int fd_rep;
    u16 anstype = envoyer_requete(pipes_dir, OPCODE_TERMINATE, NULL, &req_path, &rep_path, &fd_rep);
    
    if (anstype == 0) return;

    if(anstype != ANSTYPE_OK) perror("Erreur : échec terminaison serveur");
    fermer_pipes(req_path, rep_path, fd_rep);
}

// envoie et reçoit la réponse pour REMOVE
static void envoyer_remove(const char *pipes_dir, u64 taskid){
    char *req_path, *rep_path;
    int fd_rep;
    u16 anstype = envoyer_requete(pipes_dir, OPCODE_REMOVE, &taskid, &req_path, &rep_path, &fd_rep);
    
    if (anstype == 0) return;
    
    if(anstype != ANSTYPE_OK){
        fprintf(stderr, "Erreur: tâche non trouvée\n");
        fermer_pipes(req_path, rep_path, fd_rep);
        exit(1);
    }
    fermer_pipes(req_path, rep_path, fd_rep);

}

// parse les options de timing en timing_t
static timing_t parse_timing_opts(char *opt_minutes, char *opt_heures, char *opt_jours) {
    timing_t timing;
    timing.minutes = read_minutes_string(opt_minutes);
    timing.heures = read_heures_string(opt_heures);
    timing.jours = read_jours_string(opt_jours);
    return timing;
}

// envoie et reçoit la réponse pour CREATE
static void envoyer_create(const char *pipes_dir, char *opt_minutes, char *opt_heures, 
                    char *opt_jours, int argc, char **argv, int optind){
    char *req_path = NULL, *rep_path = NULL;
    if (setup_pipes(pipes_dir, &req_path, &rep_path) < 0) return;

    timing_t timing = parse_timing_opts(opt_minutes, opt_heures, opt_jours);

    int fd_req = open_envoi_opcode(req_path, OPCODE_CREATE);
    if (fd_req < 0) { fermer_pipes(req_path, rep_path, -1); return; }

    write_u64(fd_req, timing.minutes);
    write_u32(fd_req, timing.heures);
    write_u8(fd_req, timing.jours);

    u32 cmd_argc = argc - optind;
    write_u32(fd_req, cmd_argc);
    for (int i = optind; i < argc; i++) {
        u32 len = strlen(argv[i]);
        write_u32(fd_req, len);
        write(fd_req, argv[i], len);
    }

    close(fd_req);
    u16 anstype;
    int fd_rep = open_recevoir_anstype(rep_path, &anstype);
    if (fd_rep >= 0) {
        if (anstype == ANSTYPE_OK) {
            u64 taskid = read_u64(fd_rep);
            printf("%lu\n", taskid);
        }
        else fprintf(stderr, "Erreur création tâche\n");
    }

    fermer_pipes(req_path, rep_path, fd_rep);
}

// envoie et reçoit la réponse pour COMBINE
static void envoyer_combine(const char *pipes_dir, char *opt_minutes, char *opt_heures, 
                    char *opt_jours, int argc, char **argv, int optind, u16 type){
    char *req_path = NULL, *rep_path = NULL;
    if (setup_pipes(pipes_dir, &req_path, &rep_path) < 0) return;

    timing_t timing = parse_timing_opts(opt_minutes, opt_heures, opt_jours);

    int fd_req = open_envoi_opcode(req_path, OPCODE_COMBINE);
    if (fd_req < 0) { fermer_pipes(req_path, rep_path, -1); return; }

    write_u64(fd_req, timing.minutes);
    write_u32(fd_req, timing.heures);
    write_u8(fd_req, timing.jours);
    write_u16(fd_req, type);

    u32 num_tasks = argc - optind;
    write_u32(fd_req, num_tasks);
    for (int i = optind; i < argc; i++) {
        u64 tid = strtoull(argv[i], NULL, 10);
        write_u64(fd_req, tid);
    }

    close(fd_req);
    u16 anstype;
    int fd_rep = open_recevoir_anstype(rep_path, &anstype);
    if (fd_rep >= 0) {
        if (anstype == ANSTYPE_OK) {
            u64 taskid = read_u64(fd_rep);
            printf("%lu\n", taskid);
        }
        else fprintf(stderr, "Erreur création tâche combinée\n");
    }

    fermer_pipes(req_path, rep_path, fd_rep);
}

int main(int argc, char * argv[]){
    if (argc < 2) return 1;
    // recuperer l'utilisateur courant
    const char *user = getenv("USER");
    if (!user) user = "default";
    
    // construire le répertoire des pipes (par défaut /tmp/USER/erraid/pipes)
    char *pipes_dir = allouer_chemin(strlen("/tmp/") + strlen(user) + strlen("/erraid/pipes") + 1);
    snprintf(pipes_dir, strlen("/tmp/") + strlen(user) + strlen("/erraid/pipes") + 1, "/tmp/%s/erraid/pipes", user);

    int opt;
    int option_choisie = 0;
    //options pour create
    char *opt_minutes = NULL;
    char *opt_heures = NULL;
    char *opt_jours = NULL;
    u64 taskid = 0;

    // parser les options
    while((opt = getopt(argc, argv, "+clx:o:e:P:pqr:m:H:d:nsi")) != -1){
        // switch des options
        switch (opt){
            case 'P' :
                free(pipes_dir);
                pipes_dir = allouer_chemin(strlen(optarg) + 1);
                if (pipes_dir) strcpy(pipes_dir, optarg);
                break;
            case 'm':
                opt_minutes = optarg;  
                break;
            case 'H':
                opt_heures = optarg;
                break;
            case 'd':
                opt_jours = optarg;
                break;
            case 'n':
                opt_minutes = "-";
                opt_heures = "-";
                opt_jours = "-";
                break;
            case 'x': 
            case 'o': 
            case 'e':  
            case 'r': 
                taskid = strtoull(optarg, NULL, 10);
                /* fallthrough */
            default :
                option_choisie = opt;
                break;
        }
    }

    // exécuter l'option choisie
    switch(option_choisie){
        case 'l':
            envoyer_list(pipes_dir);
            break;
        case 'x':
            envoyer_times_exitcodes(pipes_dir, taskid);
            break;
        case 'o':
            envoyer_output(pipes_dir, taskid, OPCODE_STDOUT);
            break;
        case 'e':
            envoyer_output(pipes_dir, taskid, OPCODE_STDERR);
            break;
        case 'q':
            envoyer_terminate(pipes_dir);
            break;
        case 'r':
            envoyer_remove(pipes_dir, taskid);
            break;
        case 'c':
            envoyer_create(pipes_dir, opt_minutes, opt_heures, opt_jours, argc, argv, optind);
            break;
        case 's':
            envoyer_combine(pipes_dir, opt_minutes, opt_heures, opt_jours, argc, argv, optind, TYPE_SQ);
            break;
        case 'p':
            envoyer_combine(pipes_dir, opt_minutes, opt_heures, opt_jours, argc, argv, optind, TYPE_PL);
            break;
        case 'i':
            envoyer_combine(pipes_dir, opt_minutes, opt_heures, opt_jours, argc, argv, optind, TYPE_IF);
            break;
        default:
            // aucune option valide
            free(pipes_dir);
            return 1;
    }
    
    free(pipes_dir);
    return 0;

}