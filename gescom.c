
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <pthread.h>
#include <sys/stat.h>

#include "creme.h"
#include "gescom.h"

/* ================================================================ */
/* Variables des threads serveurs                                    */
/* ================================================================ */
static pthread_t tid_udp = 0;
static pthread_t tid_tcp = 0;

/* ================================================================ */
/* Commande interne : beuip start <pseudo> [reppub]                 */
/* ================================================================ */
int cmd_beuip_start(const char *pseudo, const char *rep)
{
    if (serveur_actif) {
        printf("Erreur: les serveurs sont déjà actifs (beuip stop d'abord).\n");
        return -1;
    }

    /* Enregistrement du pseudo et du répertoire public */
    strncpy(mon_pseudo, pseudo, LPSEUDO);
    mon_pseudo[LPSEUDO] = '\0';
    if (rep) strncpy(reppub, rep, sizeof(reppub) - 1);

    /* Création du répertoire public si absent */
    if (access(reppub, F_OK) != 0) {
        if (mkdir(reppub, 0755) < 0)
            perror("mkdir reppub");
        else
            printf("[BEUIP] Répertoire '%s' créé.\n", reppub);
    }

    /* Activation du flag avant le lancement des threads */
    serveur_actif = 1;

    /* Lancement du thread serveur UDP */
    if (pthread_create(&tid_udp, NULL, serveur_udp, NULL) != 0) {
        perror("pthread_create UDP");
        serveur_actif = 0;
        return -1;
    }

    /* Lancement du thread serveur TCP */
    if (pthread_create(&tid_tcp, NULL, serveur_tcp, (void *)reppub) != 0) {
        perror("pthread_create TCP");
        serveur_actif = 0;
        return -1;
    }

    printf("[BEUIP] Serveurs démarrés avec le pseudo '%s'.\n", mon_pseudo);
    return 0;
}

/* ================================================================ */
/* Commande interne : beuip stop                                    */
/* ================================================================ */
int cmd_beuip_stop(void)
{
    struct elt *courant;

    if (!serveur_actif) {
        printf("Erreur: aucun serveur actif.\n");
        return -1;
    }

    /* Envoi du message de départ à tous les contacts */
    pthread_mutex_lock(&mutex_liste);
    courant = liste_contacts;
    while (courant) {
        envoi_udp_unicast(CODE_DEPART, "", courant->adip);
        courant = courant->next;
    }
    pthread_mutex_unlock(&mutex_liste);

    /* Désactivation du flag d'activité */
    serveur_actif = 0;

    /* Réveil des threads bloqués sur recvfrom/accept via signal */
    pthread_cancel(tid_udp);
    pthread_cancel(tid_tcp);

    pthread_join(tid_udp, NULL);
    pthread_join(tid_tcp, NULL);
    tid_udp = 0;
    tid_tcp = 0;

    /* Nettoyage de la liste des contacts */
    videeListe();

    printf("[BEUIP] Serveurs arrêtés.\n");
    return 0;
}

/* ================================================================ */
/* Commande interne : beuip list                                    */
/* ================================================================ */
int cmd_beuip_list(void)
{
    if (!serveur_actif) {
        printf("Erreur: le serveur n'est pas actif.\n");
        return -1;
    }
    listeElts();
    return 0;
}

/* ================================================================ */
/* Commande interne : beuip message <pseudo|all> <texte>            */
/* ================================================================ */
int cmd_beuip_message(const char *destinataire, const char *message)
{
    struct elt *courant;

    if (!serveur_actif) {
        printf("Erreur: le serveur n'est pas actif.\n");
        return -1;
    }

    if (strcmp(destinataire, "all") == 0) {
        /* Envoi à tous via la liste locale (pas de datagramme '5') */
        pthread_mutex_lock(&mutex_liste);
        courant = liste_contacts;
        if (!courant)
            printf("[MSG] Aucun utilisateur à qui envoyer le message.\n");
        while (courant) {
            envoi_udp_unicast(CODE_MESSAGE, message, courant->adip);
            courant = courant->next;
        }
        pthread_mutex_unlock(&mutex_liste);
    } else {
        const char *adip = chercheAdresseParPseudo(destinataire);
        if (!adip) {
            printf("Utilisateur '%s' introuvable.\n", destinataire);
            return -1;
        }
        envoi_udp_unicast(CODE_MESSAGE, message, adip);
    }
    return 0;
}

/* ================================================================ */
/* Commande interne : beuip ls <pseudo>                             */
/* ================================================================ */
int cmd_beuip_ls(const char *pseudo)
{
    if (!serveur_actif) {
        printf("Erreur: le serveur n'est pas actif.\n");
        return -1;
    }
    demandeListe(pseudo);
    return 0;
}

/* ================================================================ */
/* Commande interne : beuip get <pseudo> <fichier>                  */
/* ================================================================ */
int cmd_beuip_get(const char *pseudo, const char *nomfic)
{
    if (!serveur_actif) {
        printf("Erreur: le serveur n'est pas actif.\n");
        return -1;
    }
    demandeFichier(pseudo, nomfic);
    return 0;
}

/* ================================================================ */
/* Dispatcher principal des commandes beuip                         */
/* ================================================================ */
int commande_beuip(int argc, char *argv[])
{
    /* argv[0] = "beuip", argv[1] = sous-commande */
    if (argc < 2) {
        printf("Usage: beuip start|stop|list|message|ls|get ...\n");
        return -1;
    }

    if (strcmp(argv[1], "start") == 0) {
        if (argc < 3) { printf("Usage: beuip start <pseudo> [reppub]\n"); return -1; }
        return cmd_beuip_start(argv[2], (argc >= 4) ? argv[3] : "pub");

    } else if (strcmp(argv[1], "stop") == 0) {
        return cmd_beuip_stop();

    } else if (strcmp(argv[1], "list") == 0) {
        return cmd_beuip_list();

    } else if (strcmp(argv[1], "message") == 0) {
        if (argc < 4) { printf("Usage: beuip message <pseudo|all> <message>\n"); return -1; }
        /* Reconstruction du message si plusieurs mots */
        static char msg_complet[TAILLE_LIGNE];
        int i;
        msg_complet[0] = '\0';
        for (i = 3; i < argc; i++) {
            if (i > 3) strncat(msg_complet, " ", sizeof(msg_complet) - strlen(msg_complet) - 1);
            strncat(msg_complet, argv[i], sizeof(msg_complet) - strlen(msg_complet) - 1);
        }
        return cmd_beuip_message(argv[2], msg_complet);

    } else if (strcmp(argv[1], "ls") == 0) {
        if (argc < 3) { printf("Usage: beuip ls <pseudo>\n"); return -1; }
        return cmd_beuip_ls(argv[2]);

    } else if (strcmp(argv[1], "get") == 0) {
        if (argc < 4) { printf("Usage: beuip get <pseudo> <fichier>\n"); return -1; }
        return cmd_beuip_get(argv[2], argv[3]);

    } else {
        printf("Commande beuip inconnue : '%s'\n", argv[1]);
        return -1;
    }
}

/* ================================================================ */
/* Exécution d'une commande externe via fork/exec                   */
/* ================================================================ */
int execute_externe(int argc, char *argv[])
{
    pid_t pid;
    int   statut;

    (void)argc;

    pid = fork();
    if (pid < 0) {
        perror("fork");
        return -1;
    }
    if (pid == 0) {
        execvp(argv[0], argv);
        perror(argv[0]);
        exit(127);
    }
    waitpid(pid, &statut, 0);
    return WIFEXITED(statut) ? WEXITSTATUS(statut) : -1;
}

/* ================================================================ */
/* Analyse d'une ligne de commande en argc/argv                     */
/* ================================================================ */
int parse_ligne(char *ligne, char *argv[], int max)
{
    int   argc = 0;
    char *p    = ligne;

    while (*p && argc < max - 1) {
        /* Passer les espaces */
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;

        argv[argc++] = p;

        /* Avancer jusqu'au prochain séparateur */
        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p) *p++ = '\0';
    }
    argv[argc] = NULL;
    return argc;
}
