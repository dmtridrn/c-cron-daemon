#ifndef PLANIFICATEUR_H
#define PLANIFICATEUR_H

#include "types.h"
#include <time.h>

// Teste si une tâche doit s'exécuter à un moment donné
// Retourne 1 si oui, 0 sinon
int devrait_sexec(timing_t *timing, struct tm *now);

#endif // PLANIFICATEUR_H