#ifndef OPERATIONSYS_H
#define OPERATIONSYS_H

#include "types.h"
#include <stddef.h>

// alloue un buffer dynamique de taille suffisante pour un chemin
char* allouer_chemin(size_t taille_base);
// crée un fichier vide (ou tronque s'il existe)
char * creer_fichier_vide(char *chemin, char *nom_fichier);
// écrit du contenu dans un fichier (écrase le contenu existant)
void ecrire_contenu_dans_fichier(const char *chemin, const char *contenu);
// écrit le timestamp et l'exitcode dans times-exitcodes
void ecrire_time_exitcode(const char *racine_tache, u16 exitcode);
// crée un répertoire et son arborescence
int creer_repertoire(const char *chemin);
//creé les tubes nommés pour communication
int creer_tubes(const char *pipes_dir, char **request_pipe, char **reply_pipe);
//supprime tout un répertoire et son contenu
int supprimer_repertoire_recursif(const char *path);
#endif