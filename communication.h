#ifndef COMMUNICATION_H
#define COMMUNICATION_H

#include "types.h"

// définition des opcodes
#define OPCODE_LIST 0x4c53      // 'LS'
#define OPCODE_CREATE 0x4352    // 'CR'
#define OPCODE_COMBINE 0x4342   // 'CB'
#define OPCODE_REMOVE 0x524d    // 'RM'
#define OPCODE_TX 0x5458        // 'TX'
#define OPCODE_STDOUT 0x534f    // 'SO'
#define OPCODE_STDERR 0x5345    // 'SE'
#define OPCODE_TERMINATE 0x544d // 'TM'

// format des réponses
#define ANSTYPE_OK 0x4f4b // 'OK'
#define ANSTYPE_ER 0x4552 // 'ER'

// format des erreurs
#define ERRCODE_NF 0x4e46 // 'NF'
#define ERRCODE_NR 0x4e52 // 'NR'

// fonction pour écrire les entiers
void write_u16(int fd, u16 value);
void write_u32(int fd, u32 value);
void write_u64(int fd, u64 value);
void write_i64(int fd, i64 value);
void write_u8(int fd, u8 value);

//fonction pour écrire un timing
void write_timing(int fd, timing_t *timing);

//fonction pour écrire un string 
void write_string(int fd, string_t *str);

//fonction pour écrire des arguments
void write_arguments(int fd, arguments_t *args);

//fonction pour écrire une commande complète
void write_command(int fd, command_t *cmd);

#endif // COMMUNICATION_H