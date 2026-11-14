#include "memoire.h"
#include "types.h"
#include <stdlib.h>

// libère une string_t
void free_string(string_t *s) {
    if (s && s->data) {
        free(s->data);
        s->data = NULL;
        s->length = 0;
    }
}

// libère un arguments_t
void free_arguments(arguments_t *args) {
    if (args && args->argv) {
        for (u32 i = 0; i < args->argc; i++) {
            free_string(&args->argv[i]);
        }
        free(args->argv);
        args->argv = NULL;
        args->argc = 0;
    }
}

// libère une command_t (récursif)
void free_command(command_t *cmd) {
    if (!cmd) return;
    
    if (cmd->nsub == 0) {
        // pour la commande simple on libère les args
        free_arguments(&cmd->args);
    } else {
        // sinon on free toutes les sous commandes récursivement
        for (u32 i = 0; i < cmd->nsub; i++) {
            free_command(&cmd->subcmds[i]);
        }
        free(cmd->subcmds);
        cmd->subcmds = NULL;
        cmd->nsub = 0;
    }
}