#include "communication.h"
#include <stdio.h>
#include <unistd.h>
#include <endian.h>   // pour be16toh, be32toh, be64toh
#include <arpa/inet.h>

// fonction pour écrire un u16
void write_u16(int fd, u16 value) {
    u16 be = htons(value);
    ssize_t nb = write(fd, &be, sizeof(u16));

    if (nb != sizeof(u16)) {
        perror("Erreur écriture u16");
    }
}

// fonction pour écrire un u8
void write_u8(int fd, u8 value) {
    ssize_t nb = write(fd, &value, sizeof(u8));
    if (nb != sizeof(u8)) { //pas besoin de big endian pour 8 bits
        perror("Erreur écriture u8");
    }
}

// fonction pour écrire un u32
void write_u32(int fd, u32 value) {
    u32 be = htonl(value);
    ssize_t nb = write(fd, &be, sizeof(u32));

    if (nb != sizeof(u32)) {
        perror("Erreur écriture u32");
    }
}

// fonction pour écrire un u64
void write_u64(int fd, u64 value) {
    u64 be = htobe64(value);
    ssize_t nb = write(fd, &be, sizeof(u64));

    if (nb != sizeof(u64)) {
        perror("Erreur écriture u64");
    }
}

// fonction pour écrire un i64
void write_i64(int fd, i64 value) {
    i64 be = htobe64(value);
    ssize_t nb = write(fd, &be, sizeof(i64));

    if (nb != sizeof(i64)) {
        perror("Erreur écriture i64");
    }
}

// fonction pour écrire un timing
void write_timing(int fd, timing_t *timing) {
    write_u64(fd, timing->minutes);
    write_u32(fd, timing->heures);
    
    ssize_t nb = write(fd, &timing->jours, sizeof(u8));

    if (nb != sizeof(u8)) {
        perror("Erreur écriture timing jours");
    }
}

// fonction pour écrire un string
void write_string(int fd, string_t *str) {
    write_u32(fd, str->length);
    ssize_t nb = write(fd, str->data, str->length);

    if (nb != (ssize_t)str->length) {
        perror("Erreur écriture string data");
    }
}

// fonction pour écrire des arguments
void write_arguments(int fd, arguments_t *args) {
    write_u32(fd, args->argc);

    for (u32 i = 0; i < args->argc; i++) {
        write_string(fd, &args->argv[i]);
    }
}

// fonction pour écrire une commande complète (récursive)
void write_command(int fd, command_t *cmd) {
    write_u16(fd, cmd->type);
    
    if (cmd->nsub == 0) {
        // Commande simple : écrire argc et argv
        write_arguments(fd, &cmd->args);
    } else {
        // Commande composée : écrire nsub puis chaque sous-commande
        write_u32(fd, cmd->nsub);
        for (u32 i = 0; i < cmd->nsub; i++) {
            write_command(fd, &cmd->subcmds[i]);
        }
    }
}