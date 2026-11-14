#ifndef RESEAU_H
#define RESEAU_H

#include "types.h"//pour u64

// fonctions pour traiter les requêtes du pipe


// lit les arguments d'une commande depuis un fichier binaire
string_t* lire_arguments(const char *argv_path, u32 *argc);
// traite la requete LIST et envoie la réponse sur le pipe
void traiter_list(const char *tasks_dir, int fd_reply);
// traite la requete TIMES_EXITCODES et envoie la réponse sur le pipe
void traiter_times_exitcodes(const char *tasks_dir, u64 taskid, int fd_reply);
// traite la requete STDOUT et envoie la réponse sur le pipe
void traiter_stdout(const char *tasks_dir, u64 taskid, int fd_reply);
// traite la requete STDERR et envoie la réponse sur le pipe
void traiter_stderr(const char *tasks_dir, u64 taskid, int fd_reply);
// traite la requete REMOVE et envoie la réponse sur le pipe
void traiter_remove(const char *tasks_dir, u64 taskid, int fd_reply);
// traite la requete CREATE et envoie la réponse sur le pipe
void traiter_create(const char *tasks_dir, int fd_req, int fd_reply);
// traite la requete COMBINE et envoie la réponse sur le pipe
void traiter_combine(const char *tasks_dir, int fd_req, int fd_reply);

#endif // RESEAU_H