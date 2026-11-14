# Planificateur de tâches

Projet de création d'un planificateur de tâches complet comprenant un démon (`erraid`) et un client de contrôle (`tadmor`). Le système permet la planification, l'exécution et le suivi de tâches (commandes simples, pipelines, séquences, conditionnelles).

## Compilation

Un `Makefile` est fourni pour la compilation du projet :

```bash
make            # Compile erraid et tadmor
make erraid     # Compile uniquement le démon
make tadmor     # Compile uniquement le client
make distclean  # Supprime les fichiers compilation (.o) et exécutables
```

## Structure

* **erraid** : Démon qui tourne en arrière-plan (par défaut) et gère l'exécution des tâches.
* **tadmor** : Client permettant d'envoyer des commandes au démon via des tubes nommés.

## Utilisation

### Le démon : erraid

Le démon doit être lancé avant toute utilisation du client.

```bash
./erraid [options]
```

**Options :**
* `-R <dir>` : Spécifie le répertoire racine de stockage (contient `tasks/` et `pipes/`). Par défaut : `/tmp/$USER/erraid`.
* `-P <dir>` : Spécifie uniquement le répertoire des tubes nommés.
* `-F` : Force l'exécution en avant-plan (ne se détache pas du terminal).

Exemple :
```bash
./erraid -F   # Utile pour voir les logs directement
```

### Le client : tadmor

Permet de manipuler les tâches gérées par le démon.

```bash
./tadmor [options] [commande]
```

**Options générales :**
* `-P <dir>` : Spécifie le répertoire des tubes nommés (si différent du défaut).

**Gestion des tâches :**
* `-l` : Lister les tâches enregistrées.
* `-c ...` : Créer une tâche (suivi des options de temps et de la commande).
* `-r <id>` : Supprimer une tâche.
* `-q` : Arrêter le démon.

**Consultation des résultats :**
* `-o <id>` : Afficher la sortie standard (stdout) de la dernière exécution.
* `-e <id>` : Afficher la sortie d'erreur (stderr).
* `-x <id>` : Afficher l'historique des codes de retour et dates d'exécution.

**Options de planification (format style cron) :**
* `-m <minutes>` : Minutes (0-59, liste séparée par virgules ou plages).
* `-H <heures>` : Heures (0-23).
* `-d <jours>` : Jours (0-6, 0=Dimanche).

**Création de tâches complexes :**
* `-s` : Séquences de commandes.
* `-p` : Pipelines (`|`).
* `-i` : Exécution conditionnelle.

Exemple création :
```bash
# Exécuter "ls -l" tous les jours à 12h30
./tadmor -c -m 30 -H 12 ls -l
```

## Tests

```bash
./test.sh
```

Projet de Système d'Exploitation - Université Paris Cité - Année 2025-2026