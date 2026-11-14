#ifndef PARSING_H
#define PARSING_H

#include "types.h"
#define CMD_TYPE_SIMPLE 0x5349


// fonctions pour lire les entiers en big-endian et les convertir
u16 read_u16(int fd);
u32 read_u32(int fd);
u64 read_u64(int fd);
i64 read_i64(int fd);

// fonction pour convertir le contenu d'un fichier en string_t
string_t read_string(int fd);

// fonction pour convertir un timing lu depuis un fichier
timing_t read_timing(int fd);

// fonction pour lire une commande depuis l'arborescence
command_t *read_command(const char *cmd_path);

// fonction pour lire une commande depuis un file descriptor (protocole)
command_t *read_command_from_fd(int fd);

// fonction pour lire toutes les entrées d'un fichier times-exitcodes depuis le chemin
time_exitcode_t* read_time_exitcodes(const char *filepath, u32 *count);

// fonction pour vérifier si une chaîne est numérique
int est_numerique(const char *nom);

//fonction pour lire le timing (-m [minutes] -H [heures] -d [jours] )
u64 read_minutes_string(const char *str);
u32 read_heures_string(const char *str);
u8 read_jours_string(const char *str);

#endif // PARSING_H