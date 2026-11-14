#ifndef MEMORY_H
#define MEMORY_H

#include "types.h"

// libère une string_t
void free_string(string_t *s);

// libère un arguments_t
void free_arguments(arguments_t *args);

// libère une command_t (récursif)
void free_command(command_t *cmd);

#endif 