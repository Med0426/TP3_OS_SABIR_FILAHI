
#ifndef CREME_H
#define CREME_H

#include <pthread.h>
#include <netinet/in.h>

/* ---- Paramètres réseau ---- */
#define PORT_BEUIP       9998
#define ADRESSE_BROADCAST "192.168.88.255"
#define TAILLE_MAX_MSG   512
#define LPSEUDO          23    /* longueur maxi du pseudo */

/* ---- Codes protocole BEUIP (octet1) ---- */
#define CODE_DEPART      '0'   /* déconnexion */
#define CODE_PRESENCE    '1'   /* annonce de présence (broadcast) */
#define CODE_ACK         '2'   /* accusé de réception */
#define CODE_LISTE       '3'   /* demande de liste (obsolète, désactivé) */
#define CODE_MSG_UN      '4'   /* message ciblé (obsolète, désactivé) */
#define CODE_MSG_TOUS    '5'   /* message à tous (obsolète, désactivé) */
#define CODE_MESSAGE     '9'   /* message unicast sécurisé */

/* ---- Codes protocole TCP ---- */
#define TCP_LISTE        'L'   /* demande de liste de fichiers */
#define TCP_FICHIER      'F'   /* demande d'un fichier */

/* ---- Structure d'un élément de la liste chaînée des contacts ---- */
struct elt {
    char       nom[LPSEUDO + 1]; /* pseudo de l'utilisateur    */
    char       adip[16];         /* adresse IPv4 sous forme str */
    struct elt *next;            /* pointeur vers l'élément suivant */
};

/* ---- Variables globales partagées (définies dans creme.c) ---- */
extern struct elt      *liste_contacts;  /* tête de liste chaînée    */
extern pthread_mutex_t  mutex_liste;     /* verrou pour la liste      */
extern volatile int     serveur_actif;   /* flag d'état du serveur    */
extern char             mon_pseudo[LPSEUDO + 1]; /* pseudo local      */
extern char             reppub[256];     /* répertoire public TCP     */

/* ---- Fonctions de gestion de la liste chaînée ---- */
void ajouteElt(const char *pseudo, const char *adip);
void supprimeElt(const char *adip);
void listeElts(void);
void videeListe(void);
const char *chercheAdresseParPseudo(const char *pseudo);

/* ---- Fonctions serveur (threads) ---- */
void *serveur_udp(void *p);
void *serveur_tcp(void *rep);

/* ---- Fonctions d'envoi réseau ---- */
int  envoi_udp_broadcast(char octet1, const char *message);
int  envoi_udp_unicast(char octet1, const char *message, const char *adip);

/* ---- Fonctions client TCP ---- */
void demandeListe(const char *pseudo);
void demandeFichier(const char *pseudo, const char *nomfic);

/* ---- Détection automatique des interfaces ---- */
int  envoi_broadcast_interfaces(char octet1, const char *message);

#endif /* CREME_H */
