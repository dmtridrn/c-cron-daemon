#include "planificateur.h"
#include <stdio.h>
#include <string.h>

//vérifie si une tache doit s'executer à un moment donné
int devrait_sexec(timing_t *timing, struct tm *now) {
    // vérifie minute
    int minute_courante = now->tm_min;  // 0-59
    if (!(timing->minutes & (1ULL << minute_courante))) { //1ULL car long
        return 0;
    }
    
    //heure
    int heure_courante = now->tm_hour;   // 0-23
    if (!(timing->heures & (1U << heure_courante))) {
        return 0;
    }
    
    //jour
    int jour_courant = now->tm_wday;    // 0=dimanche, 6=samedi
    if (!(timing->jours & (1U << jour_courant))) {
        return 0; 
    }
    
    return 1;
}

