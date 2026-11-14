#ifndef TYPES_H
#define TYPES_H

#include <stdint.h>

// raccourcis plus simple a lire pour les types entiers
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int64_t  i64; //pour les timestamps

// Type string : LENGTH <uint32> + DATA <char[]>
typedef struct {
    u32 length;      // longueur sans le 0 terminal
    char *data;      // contenu
} string_t;

typedef struct{
    u64 minutes; //on utilise les 60 premiers
    u32 heures; //on utilise les 24 premiers
    u8 jours; //on utilise les 7 premiers
} timing_t;

typedef struct {
    u32 argc; // nombre d'arguments (toujours >= 1)
    string_t *argv; // tableau de chaînes (argv[0] -> nom de la commande)
} arguments_t ;

// Structure des commandes de manière récursive
typedef struct command {
    u16 type;           // valeur lue dans cmd/type
    // si type == 'SI': utilise args
    arguments_t args;

    // sinon : sous-commandes
    u32 nsub;
    struct command *subcmds; // malloc(nsub sizeof(command))
} command_t;

typedef struct {
    i64 time; // timestamp de la dernière modification
    u16 exitcode; // code de sortie de la dernière exécution
} time_exitcode_t;

typedef struct { //struct pour chopper les infos des taches pour les requetes
    u64 taskid;
    timing_t timing;
    command_t *cmd;  // Pointeur vers la commande parsée
} task_info_t;

#endif // TYPES_H

