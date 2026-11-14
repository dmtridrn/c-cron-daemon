#ifndef EXECUTEUR_H
#define EXECUTEUR_H

#include "types.h"
#include <time.h>

//  types de commandes
#define CMD_TYPE_SIMPLE 0x5349 //  simple
#define TYPE_SQ 0x5351 // séquence
#define TYPE_PL 0x504C // pipeline
#define TYPE_IF 0x4946 // conditionnelle 

//execute une commande à partir de ses argumentscontenus dans sa struct
u16 executer_commande_simple(arguments_t *args, const char *racine_tache);

// execute une commande simple ou sq a partir de sa struture parsée par parsing.c
u16 executer_commande(command_t *cmd, const char *racine_tache);

// execute une commande pipeline ou pl
u16 executer_pipeline(command_t *cmd, const char *racine_tache);

// execute une commande conditionnelle (if/then/else)
u16 executer_conditionnelle(command_t *cmd, const char *racine_tache);

// fonction pour vérifier et exécuter une tâche si nécessaire
void verifier_tache(const char *tasks_dir, const char *id_tache, struct tm *maintenant);



#endif // EXECUTEUR_H