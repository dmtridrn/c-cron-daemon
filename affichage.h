#ifndef AFFICHAGE_H
#define AFFICHAGE_H

#include "types.h"
#include <stdio.h>

// fonction pour convertir un timing en chaîne formatée
string_t time_to_string(const timing_t *timing);

// fonction pour convertir un timing en format crontab (ex: "1,3-6,9-15 * *")
string_t timing_to_crontab_string(const timing_t *timing);

// fonction pour convertir une commande en string lisible
string_t command_to_string(const command_t *cmd);

// fonction pour convertir un timestamp (i64) en string formaté (jour mois année heure:minute:seconde)
string_t secondes_vers_string(i64 timestamp);

// fonction pour convertir un uint16 en string
string_t uint_vers_string(u16 value);

// affiche le contenu d'un fichier dans le terminal
void afficher_dans_terminal(char *chemin, char * nom_fichier);

// pour lister toutes les tâches disponibles
void lister_taches(const char *tasks_dir);

// affiche une tâche
void afficher_tache(const char *id, const char *tasks_dir);

// affiche une commande de manière récursive
void afficher_cmd(const command_t *cmd, int indent);

// manuel du programme
void afficher_usage(const char *prog_name);

#endif