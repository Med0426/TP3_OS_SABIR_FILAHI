
#ifndef GESCOM_H
#define GESCOM_H

/* Nombre maximal d'arguments sur une ligne de commande */
#define MAX_ARGS 16

/* Taille maximale d'une ligne saisie */
#define TAILLE_LIGNE 512

/* ---- Prototypes des gestionnaires de commandes internes ---- */

/* Lance les threads serveurs UDP et TCP */
int cmd_beuip_start(const char *pseudo, const char *rep);

/* Arrête les threads serveurs */
int cmd_beuip_stop(void);

/* Affiche la liste des utilisateurs connectés */
int cmd_beuip_list(void);

/* Envoie un message à un pseudo ou à "all" */
int cmd_beuip_message(const char *destinataire, const char *message);

/* Demande la liste des fichiers d'un pair TCP */
int cmd_beuip_ls(const char *pseudo);

/* Télécharge un fichier depuis un pair TCP */
int cmd_beuip_get(const char *pseudo, const char *nomfic);

/* Interprète et dispatch une commande beuip */
int commande_beuip(int argc, char *argv[]);

/* Exécute une commande externe via fork/exec */
int execute_externe(int argc, char *argv[]);

/* Analyse une ligne et remplit argv[] - retourne argc */
int parse_ligne(char *ligne, char *argv[], int max);

#endif /* GESCOM_H */
