                        

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#include "creme.h"
#include "gescom.h"

/* ================================================================ */
/* Constantes du shell                                              */
/* ================================================================ */
#define PROMPT       "biceps> "
#define NOM_SHELL    "biceps"
#define VERSION      "3.0"

/* ================================================================ */
/* Gestionnaire de signal SIGINT (Ctrl+C)                           */
/* ================================================================ */
static void gestionnaire_sigint(int sig)
{
    (void)sig;
    /* Affichage d'une nouvelle ligne et du prompt */
    write(STDOUT_FILENO, "\n", 1);
    write(STDOUT_FILENO, PROMPT, sizeof(PROMPT) - 1);
}

/* ================================================================ */
/* Vérification des commandes internes de biceps                    */
/* ================================================================ */
static int est_commande_interne(const char *cmd)
{
    return (strcmp(cmd, "beuip") == 0 ||
            strcmp(cmd, "exit")  == 0 ||
            strcmp(cmd, "quit")  == 0 ||
            strcmp(cmd, "aide")  == 0 ||
            strcmp(cmd, "help")  == 0);
}

/* ================================================================ */
/* Affichage de l'aide                                              */
/* ================================================================ */
static void affiche_aide(void)
{
    printf("=== biceps v%s - Aide des commandes internes ===\n", VERSION);
    printf("  beuip start <pseudo> [reppub]  : Démarrer les serveurs UDP/TCP\n");
    printf("  beuip stop                     : Arrêter les serveurs\n");
    printf("  beuip list                     : Lister les utilisateurs\n");
    printf("  beuip message <pseudo|all> <msg>: Envoyer un message\n");
    printf("  beuip ls <pseudo>              : Lister les fichiers d'un pair\n");
    printf("  beuip get <pseudo> <fichier>   : Télécharger un fichier\n");
    printf("  aide / help                    : Afficher cette aide\n");
    printf("  exit / quit                    : Quitter biceps\n");
    printf("  <commande>                     : Exécuter une commande système\n");
    printf("================================================\n");
}

/* ================================================================ */
/* Traitement d'une commande interne                                */
/* ================================================================ */
static int traite_interne(int argc, char *argv[])
{
    const char *cmd = argv[0];

    if (strcmp(cmd, "exit") == 0 || strcmp(cmd, "quit") == 0)
        return -2; /* code de sortie du shell */

    if (strcmp(cmd, "aide") == 0 || strcmp(cmd, "help") == 0) {
        affiche_aide();
        return 0;
    }

    if (strcmp(cmd, "beuip") == 0)
        return commande_beuip(argc, argv);

    return -1;
}

/* ================================================================ */
/* Boucle principale du shell                                       */
/* ================================================================ */
static void boucle_shell(void)
{
    char  ligne[TAILLE_LIGNE];
    char *argv[MAX_ARGS];
    int   argc, ret;

    affiche_aide();
    printf("\nBienvenue dans biceps v%s !\n\n", VERSION);

    while (1) {
        /* Affichage du prompt */
        printf("%s", PROMPT);
        fflush(stdout);

        /* Lecture de la ligne */
        if (!fgets(ligne, sizeof(ligne), stdin)) {
            /* EOF : on quitte proprement */
            printf("\n");
            break;
        }

        /* Suppression du '\n' final */
        ligne[strcspn(ligne, "\n")] = '\0';

        /* Ignorer les lignes vides ou commentaires */
        if (ligne[0] == '\0' || ligne[0] == '#') continue;

        /* Analyse de la ligne */
        argc = parse_ligne(ligne, argv, MAX_ARGS);
        if (argc == 0) continue;

#ifdef TRACE
        printf("[TRACE shell] argc=%d, argv[0]='%s'\n", argc, argv[0]);
#endif

        /* Dispatch : interne ou externe */
        if (est_commande_interne(argv[0])) {
            ret = traite_interne(argc, argv);
            if (ret == -2) break; /* exit/quit */
        } else {
            ret = execute_externe(argc, argv);
        }

#ifdef TRACE2
        printf("[TRACE2 shell] retour = %d\n", ret);
#endif
        (void)ret;
    }
}

/* ================================================================ */
/* Point d'entrée principal                                         */
/* ================================================================ */
int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    /* Installation du gestionnaire pour Ctrl+C */
    signal(SIGINT, gestionnaire_sigint);

    /* Ignorer SIGPIPE (connexions TCP fermées côté client) */
    signal(SIGPIPE, SIG_IGN);

    printf("=== %s version %s ===\n", NOM_SHELL, VERSION);
    printf("Cours OS User - Polytech Sorbonne\n\n");

    /* Lancement de la boucle principale */
    boucle_shell();

    /* Arrêt propre des serveurs si encore actifs */
    if (serveur_actif)
        cmd_beuip_stop();

    printf("Au revoir !\n");
    return 0;
}
