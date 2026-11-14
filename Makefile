CC=gcc
CFLAGS=-Wall -Wextra -g 

all: erraid tadmor

erraid: erraid.c executeur.c parsing.c planificateur.c memoire.c operationsys.c affichage.c reseau.c communication.c
	$(CC) $(CFLAGS) -o erraid erraid.c executeur.c parsing.c planificateur.c memoire.c operationsys.c affichage.c reseau.c communication.c

tadmor: tadmor.c parsing.c communication.c memoire.c operationsys.c  affichage.c
	$(CC) $(CFLAGS) -o tadmor tadmor.c parsing.c communication.c memoire.c operationsys.c  affichage.c

distclean:
	rm -f erraid tadmor *.o

