#include "affichage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>

#include "parsing.h"
#include "memoire.h"
#include "operationsys.h"
#include "executeur.h"


// formate des bits en chaîne crontab : "-" si vide, "*" si tous activés, sinon liste/plages
void format_bits_timing(char *buf, size_t bufsize, u64 bits, int max_bit) {
    int pos = 0;
    
    // vérifier si tous les bits sont à 0
    u64 mask = (max_bit == 64) ? ~0ULL : ((1ULL << max_bit) - 1);
    if ((bits & mask) == 0) {
        buf[0] = '-';
        buf[1] = '\0';
        return;
    }
    
    // vérifier si tous les bits sont à 1
    if ((bits & mask) == mask) {
        buf[0] = '*';
        buf[1] = '\0';
        return;
    }
    
    int premier = 1;
    int i = 0;
    while (i < max_bit) {
        if (bits & (1ULL << i)) {
            int debut = i;
            // chercher la fin du range
            while (i + 1 < max_bit && (bits & (1ULL << (i + 1)))) {i++;}
            int fin = i;
            if (!premier) {pos += snprintf(buf + pos, bufsize - pos, ",");}
            premier = 0;
            
            if (debut == fin) {pos += snprintf(buf + pos, bufsize - pos, "%d", debut);}
            else {pos += snprintf(buf + pos, bufsize - pos, "%d-%d", debut, fin);}
        }
        i++;
    }
    buf[pos] = '\0';
}

// convertit un timing en format crontab
string_t timing_to_crontab_string(const timing_t *timing) {
    string_t result;
    char minutes_buf[256];
    char heures_buf[128];
    char jours_buf[32];
    char buffer[512];
    
    format_bits_timing(minutes_buf, sizeof(minutes_buf), timing->minutes, 60);
    format_bits_timing(heures_buf, sizeof(heures_buf), timing->heures, 24);
    format_bits_timing(jours_buf, sizeof(jours_buf), timing->jours, 7);
    
    snprintf(buffer, sizeof(buffer), "%s %s %s", minutes_buf, heures_buf, jours_buf);
    
    result.length = strlen(buffer);
    result.data = malloc(result.length + 1);
    if (!result.data) {
        result.length = 0;
        return result;
    }
    strcpy(result.data, buffer);
    return result;
}

// fonc recursive pour construire la string d'une commande
void command_to_string_aux(const command_t *cmd, char *buf, size_t bufsize, int *pos) {
    if (!cmd) return;
    
    if (cmd->type == CMD_TYPE_SIMPLE) {
        for (u32 i = 0; i < cmd->args.argc; i++) {
            if (i > 0) {
                *pos += snprintf(buf + *pos, bufsize - *pos, " ");
            }
            *pos += snprintf(buf + *pos, bufsize - *pos, "%.*s", 
                           (int)cmd->args.argv[i].length, cmd->args.argv[i].data);
        }
    } 
    

    else if (cmd->type == TYPE_IF) {
        // on gère le cas conditionnel
        *pos += snprintf(buf + *pos, bufsize - *pos, "if ");
        
        // la condition a.k.a cmd[0]
        if (cmd->subcmds[0].type != CMD_TYPE_SIMPLE) {
            *pos += snprintf(buf + *pos, bufsize - *pos, "( ");
            command_to_string_aux(&cmd->subcmds[0], buf, bufsize, pos);
            *pos += snprintf(buf + *pos, bufsize - *pos, " )");
        } else {
            command_to_string_aux(&cmd->subcmds[0], buf, bufsize, pos);
            *pos += snprintf(buf + *pos, bufsize - *pos, " ;");
        }
        
        *pos += snprintf(buf + *pos, bufsize - *pos, " then ");
        
        // la branche Then s/o cmd[1]
        if (cmd->subcmds[1].type != CMD_TYPE_SIMPLE) {
            *pos += snprintf(buf + *pos, bufsize - *pos, "( ");
            command_to_string_aux(&cmd->subcmds[1], buf, bufsize, pos);
            *pos += snprintf(buf + *pos, bufsize - *pos, " )");
        } else {
            command_to_string_aux(&cmd->subcmds[1], buf, bufsize, pos);
            if (cmd->nsub >= 3) { // Semicolon si on a un ELSE après
                *pos += snprintf(buf + *pos, bufsize - *pos, " ;");
            }
        }
        
        // la branche Else donc cmd[2] si il existe
        if (cmd->nsub >= 3) {
            *pos += snprintf(buf + *pos, bufsize - *pos, " else ");
            if (cmd->subcmds[2].type != CMD_TYPE_SIMPLE) {
                *pos += snprintf(buf + *pos, bufsize - *pos, "( ");
                command_to_string_aux(&cmd->subcmds[2], buf, bufsize, pos);
                *pos += snprintf(buf + *pos, bufsize - *pos, " )");
            } else {
                command_to_string_aux(&cmd->subcmds[2], buf, bufsize, pos);
            }
        }
        
        // si la dernière commande était simple -> ; et fi 
        if ((cmd->nsub >= 3 && cmd->subcmds[2].type == CMD_TYPE_SIMPLE) ||
            (cmd->nsub < 3 && cmd->subcmds[1].type == CMD_TYPE_SIMPLE)) {
            *pos += snprintf(buf + *pos, bufsize - *pos, " ;");
        }
        *pos += snprintf(buf + *pos, bufsize - *pos, " fi");
    }
    // Fin AJOUT
    else {
        // commande composite
        char separe = ';';
        if (cmd->type == 0x504c) { // 'PL' pipeline
            separe = '|';
        }
        
        for (u32 i = 0; i < cmd->nsub; i++) {
            if (i > 0) {
                *pos += snprintf(buf + *pos, bufsize - *pos, " %c ", separe);
            }
            
            // si sous-commande est composite, on met des parenthèses
            if (cmd->subcmds[i].type != CMD_TYPE_SIMPLE) {
                *pos += snprintf(buf + *pos, bufsize - *pos, "( ");
                command_to_string_aux(&cmd->subcmds[i], buf, bufsize, pos);
                *pos += snprintf(buf + *pos, bufsize - *pos, " )");
            } else {
                command_to_string_aux(&cmd->subcmds[i], buf, bufsize, pos);
            }
        }
    }
}

// convertit une commande en string lisible
string_t command_to_string(const command_t *cmd) {
    string_t result = {0, NULL};
    char buffer[4096];
    int pos = 0;
    
    command_to_string_aux(cmd, buffer, sizeof(buffer), &pos);
    buffer[pos] = '\0';
    
    result.length = strlen(buffer);
    result.data = malloc(result.length + 1);
    if (!result.data) {
        result.length = 0;
        return result;
    }
    strcpy(result.data, buffer);
    return result;
}


// manuel du programme
void afficher_usage(const char *prog_name) {
    fprintf(stderr, "Usage: %s [-R <chemin>] [-P <pipes_dir>] [-F]\n", prog_name);
    fprintf(stderr, "  -R <chemin>     : Lance avec le répertoire racine passé en paramètre\n");
    fprintf(stderr, "                    (par défaut: /tmp/$USER/erraid)\n");
    fprintf(stderr, "  -P <pipes_dir>  : Définit le répertoire des tubes nommés\n");
    fprintf(stderr, "                    (par défaut: <racine>/pipes)\n");
    fprintf(stderr, "  -F              : Exécution en avant-plan sans démonisation\n");
    exit(EXIT_FAILURE);
}

// convertit un timing en chaîne formatée
string_t time_to_string(const timing_t *timing) {
    string_t result;
    char buffer[256];
    int pos = 0;
    
    // minutes
    int nb_minutes = 0;
    for (int i = 0; i < 60; i++) {
        if (timing->minutes & (1ULL << i)) {
            nb_minutes++;
        }
    }
    
    // heures
    int nb_heures = 0;
    for (int i = 0; i < 24; i++) {
        if (timing->heures & (1U << i)) {
            nb_heures++;
        }
    }
    
    // jours
    int nb_jours = 0;
    for (int i = 0; i < 7; i++) {
        if (timing->jours & (1U << i)) {
            nb_jours++;
        }
    }
    
    // construire la chaine
    if (nb_minutes > 0) {
        pos += snprintf(buffer + pos, sizeof(buffer) - pos, "%dm", nb_minutes);
    }
    
    if (nb_heures > 0) {
        if (pos > 0) {
            pos += snprintf(buffer + pos, sizeof(buffer) - pos, " ");
        }
        pos += snprintf(buffer + pos, sizeof(buffer) - pos, "%dh", nb_heures);
    }
    
    if (nb_jours > 0) {
        if (pos > 0) {
            pos += snprintf(buffer + pos, sizeof(buffer) - pos, " ");
        }
        pos += snprintf(buffer + pos, sizeof(buffer) - pos, "%dj", nb_jours);
    }
    
    // si rien n'est activé
    if (pos == 0) {
        snprintf(buffer, sizeof(buffer), "0m 0h 0j");
        pos = strlen(buffer);
    }
    
    // allouer et copier le résultat
    result.length = pos;
    result.data = malloc(result.length + 1);
    if (result.data == NULL) {
        perror("Erreur malloc time_to_string");
        exit(EXIT_FAILURE);
    }
    
    strcpy(result.data, buffer);
    return result;
}

// convertit un timestamp (i64) en string formaté (jour mois année heure:minute:seconde)
string_t secondes_vers_string(i64 timestamp) {
    string_t result;
    char buffer[64];
    
    // convertir le timestamp en time_t
    time_t t = (time_t)timestamp;
    struct tm *tm_info = localtime(&t);
    
    if (tm_info == NULL) {
        // en cas d'erreur, retourner une chaîne d'erreur
        snprintf(buffer, sizeof(buffer), "[timestamp invalide: %ld]", timestamp);
    } else {
        // format: "DD/MM/YYYY HH:MM:SS"
        snprintf(buffer, sizeof(buffer), "%02d/%02d/%04d %02d:%02d:%02d",
                 tm_info->tm_mday,
                 tm_info->tm_mon + 1,  // tm_mon est 0-11
                 tm_info->tm_year + 1900,  // tm_year est années depuis 1900
                 tm_info->tm_hour,
                 tm_info->tm_min,
                 tm_info->tm_sec);
    }
    
    result.length = strlen(buffer);
    result.data = malloc(result.length + 1);
    if (result.data == NULL) {
        perror("Erreur malloc secondes_vers_string");
        result.length = 0;
        return result;
    }
    strcpy(result.data, buffer);
    
    return result;
}

// convertit un uint16 en string
string_t uint_vers_string(u16 value) {
    string_t result;
    char buffer[16];
    
    snprintf(buffer, sizeof(buffer), "%u", value);
    
    result.length = strlen(buffer);
    result.data = malloc(result.length + 1);
    if (result.data == NULL) {
        perror("Erreur malloc uint_vers_string");
        result.length = 0;
        return result;
    }
    strcpy(result.data, buffer);
    
    return result;
}