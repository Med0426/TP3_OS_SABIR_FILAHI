
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <fcntl.h>
#include <pthread.h>

#include "creme.h"

/* ================================================================ */
/* Variables globales partagées entre threads                        */
/* ================================================================ */
struct elt      *liste_contacts = NULL;
pthread_mutex_t  mutex_liste    = PTHREAD_MUTEX_INITIALIZER;
volatile int     serveur_actif  = 0;
char             mon_pseudo[LPSEUDO + 1] = {0};
char             reppub[256]             = "pub";

/* ================================================================ */
/* Gestion de la liste chaînée des contacts (triée par pseudo)      */
/* ================================================================ */

/* Ajoute ou met à jour un contact dans la liste triée */
void ajouteElt(const char *pseudo, const char *adip)
{
    struct elt *nouveau, *courant, *precedent;

    if (!pseudo || !adip) return;

    /* Si le pseudo est le nôtre, on ignore */
    if (strncmp(pseudo, mon_pseudo, LPSEUDO) == 0) return;

    pthread_mutex_lock(&mutex_liste);

    /* Vérifier si l'IP ou le pseudo existe déjà (mise à jour) */
    for (courant = liste_contacts; courant; courant = courant->next) {
        if (strncmp(courant->adip, adip, 15) == 0) {
            strncpy(courant->nom, pseudo, LPSEUDO);
            courant->nom[LPSEUDO] = '\0';
            pthread_mutex_unlock(&mutex_liste);
            return;
        }
    }

    /* Allocation du nouvel élément */
    nouveau = malloc(sizeof(struct elt));
    if (!nouveau) {
        pthread_mutex_unlock(&mutex_liste);
        perror("malloc ajouteElt");
        return;
    }
    strncpy(nouveau->nom,  pseudo, LPSEUDO);
    strncpy(nouveau->adip, adip,   15);
    nouveau->nom[LPSEUDO] = '\0';
    nouveau->adip[15]     = '\0';
    nouveau->next         = NULL;

    /* Insertion triée par ordre alphabétique du pseudo */
    precedent = NULL;
    courant   = liste_contacts;
    while (courant && strncmp(courant->nom, pseudo, LPSEUDO) < 0) {
        precedent = courant;
        courant   = courant->next;
    }
    nouveau->next = courant;
    if (precedent)
        precedent->next = nouveau;
    else
        liste_contacts = nouveau;

    pthread_mutex_unlock(&mutex_liste);

#ifdef TRACE
    printf("[TRACE] ajouteElt : %s (%s)\n", pseudo, adip);
#endif
}

/* Supprime le contact ayant l'adresse IP donnée */
void supprimeElt(const char *adip)
{
    struct elt *courant, *precedent;

    if (!adip) return;

    pthread_mutex_lock(&mutex_liste);
    precedent = NULL;
    courant   = liste_contacts;
    while (courant) {
        if (strncmp(courant->adip, adip, 15) == 0) {
            if (precedent)
                precedent->next = courant->next;
            else
                liste_contacts = courant->next;
#ifdef TRACE
            printf("[TRACE] supprimeElt : %s (%s)\n", courant->nom, adip);
#endif
            free(courant);
            pthread_mutex_unlock(&mutex_liste);
            return;
        }
        precedent = courant;
        courant   = courant->next;
    }
    pthread_mutex_unlock(&mutex_liste);
}

/* Affiche la liste des contacts (format : IP : pseudo) */
void listeElts(void)
{
    struct elt *courant;

    pthread_mutex_lock(&mutex_liste);
    courant = liste_contacts;
    if (!courant) {
        printf("Aucun utilisateur connecté.\n");
    } else {
        while (courant) {
            printf("%s : %s\n", courant->adip, courant->nom);
            courant = courant->next;
        }
    }
    pthread_mutex_unlock(&mutex_liste);
}

/* Libère toute la liste chaînée */
void videeListe(void)
{
    struct elt *courant, *suivant;

    pthread_mutex_lock(&mutex_liste);
    courant = liste_contacts;
    while (courant) {
        suivant = courant->next;
        free(courant);
        courant = suivant;
    }
    liste_contacts = NULL;
    pthread_mutex_unlock(&mutex_liste);
}

/* Retourne l'adresse IP d'un pseudo (NULL si absent) - appelé sans verrou */
const char *chercheAdresseParPseudo(const char *pseudo)
{
    struct elt *courant;

    if (!pseudo) return NULL;
    pthread_mutex_lock(&mutex_liste);
    courant = liste_contacts;
    while (courant) {
        if (strncmp(courant->nom, pseudo, LPSEUDO) == 0) {
            pthread_mutex_unlock(&mutex_liste);
            return courant->adip;
        }
        courant = courant->next;
    }
    pthread_mutex_unlock(&mutex_liste);
    return NULL;
}

/* ================================================================ */
/* Envoi UDP                                                         */
/* ================================================================ */

/* Envoi d'un datagramme UDP vers une adresse broadcast donnée */
static int envoi_udp_vers(char octet1, const char *message,
                           const char *adresse)
{
    int                sock, ret;
    struct sockaddr_in dest;
    char               buf[TAILLE_MAX_MSG];
    int                opt = 1;
    size_t             len;

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { perror("socket envoi_udp"); return -1; }

    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &opt, sizeof(opt));

    memset(&dest, 0, sizeof(dest));
    dest.sin_family      = AF_INET;
    dest.sin_port        = htons(PORT_BEUIP);
    inet_pton(AF_INET, adresse, &dest.sin_addr);

    /* Format : octet1 + ':' + pseudo + ':' + message */
    if (message && strlen(message) > 0)
        snprintf(buf, sizeof(buf), "%c:%s:%s", octet1, mon_pseudo, message);
    else
        snprintf(buf, sizeof(buf), "%c:%s:", octet1, mon_pseudo);

    len = strlen(buf) + 1;
    ret = sendto(sock, buf, len, 0,
                 (struct sockaddr *)&dest, sizeof(dest));
    if (ret < 0) perror("sendto envoi_udp");

    close(sock);
    return (ret < 0) ? -1 : 0;
}

/* Envoi broadcast sur l'adresse fixe de configuration */
int envoi_udp_broadcast(char octet1, const char *message)
{
    return envoi_udp_vers(octet1, message, ADRESSE_BROADCAST);
}

/* Envoi unicast vers une adresse IP précise */
int envoi_udp_unicast(char octet1, const char *message, const char *adip)
{
    return envoi_udp_vers(octet1, message, adip);
}

/* Envoi broadcast sur TOUTES les interfaces actives (hors loopback) */
int envoi_broadcast_interfaces(char octet1, const char *message)
{
    struct ifaddrs *ifap, *ifa;
    char            hote[NI_MAXHOST];
    int             nb = 0;

    if (getifaddrs(&ifap) < 0) {
        perror("getifaddrs");
        return envoi_udp_broadcast(octet1, message); /* repli */
    }

    for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || !ifa->ifa_broadaddr) continue;
        if (ifa->ifa_addr->sa_family != AF_INET)   continue;

        /* Récupération de l'adresse broadcast */
        if (getnameinfo(ifa->ifa_broadaddr,
                        sizeof(struct sockaddr_in),
                        hote, sizeof(hote),
                        NULL, 0,
                        NI_NUMERICHOST) != 0) continue;

        /* On ignore localhost */
        if (strcmp(hote, "127.0.0.1") == 0 ||
            strcmp(hote, "127.255.255.255") == 0) continue;

#ifdef TRACE
        printf("[TRACE] broadcast sur interface %s -> %s\n",
               ifa->ifa_name, hote);
#endif
        envoi_udp_vers(octet1, message, hote);
        nb++;
    }
    freeifaddrs(ifap);

    /* Si aucune interface trouvée, utiliser l'adresse par défaut */
    if (nb == 0)
        return envoi_udp_broadcast(octet1, message);
    return 0;
}

/* ================================================================ */
/* Thread serveur UDP                                               */
/* ================================================================ */

/* Analyse et traite un datagramme reçu */
static void traite_datagramme(const char *buf, const char *adip_expediteur)
{
    char   octet1, pseudo[LPSEUDO + 1], message[TAILLE_MAX_MSG];
    char   copie[TAILLE_MAX_MSG];
    char  *tok, *save;

    if (!buf || strlen(buf) < 3) return;

    strncpy(copie, buf, sizeof(copie) - 1);
    copie[sizeof(copie) - 1] = '\0';

    /* Format attendu : octet1:pseudo:message */
    tok = strtok_r(copie, ":", &save);
    if (!tok) return;
    octet1 = tok[0];

    tok = strtok_r(NULL, ":", &save);
    if (!tok) return;
    strncpy(pseudo, tok, LPSEUDO);
    pseudo[LPSEUDO] = '\0';

    tok = strtok_r(NULL, "", &save);
    if (tok) strncpy(message, tok, sizeof(message) - 1);
    else     message[0] = '\0';
    message[sizeof(message) - 1] = '\0';

    switch (octet1) {
    case CODE_PRESENCE: /* '1' : quelqu'un arrive */
#ifdef TRACE
        printf("[TRACE] PRESENCE reçue de %s (%s)\n", pseudo, adip_expediteur);
#endif
        ajouteElt(pseudo, adip_expediteur);
        /* Répondre en unicast avec notre présence */
        envoi_udp_unicast(CODE_PRESENCE, "", adip_expediteur);
        printf("\n[BEUIP] %s (%s) vient de rejoindre le réseau.\n",
               pseudo, adip_expediteur);
        fflush(stdout);
        break;

    case CODE_DEPART: /* '0' : quelqu'un part */
#ifdef TRACE
        printf("[TRACE] DEPART reçu de %s (%s)\n", pseudo, adip_expediteur);
#endif
        supprimeElt(adip_expediteur);
        printf("\n[BEUIP] %s (%s) vient de quitter le réseau.\n",
               pseudo, adip_expediteur);
        fflush(stdout);
        break;

    case CODE_ACK: /* '2' : accusé de réception */
#ifdef TRACE
        printf("[TRACE] ACK reçu de %s\n", pseudo);
#endif
        ajouteElt(pseudo, adip_expediteur);
        break;

    case CODE_MESSAGE: /* '9' : message unicast */
        printf("\n[MSG de %s] %s\n", pseudo, message);
        fflush(stdout);
        break;

    /* Codes obsolètes/non autorisés : signaler tentative de piratage */
    case CODE_LISTE:
    case CODE_MSG_UN:
    case CODE_MSG_TOUS:
        fprintf(stderr,
                "[SECURITE] Tentative suspecte : octet1='%c' de %s\n",
                octet1, adip_expediteur);
        break;

    default:
        fprintf(stderr,
                "[SECURITE] Code inconnu '%c' reçu de %s\n",
                octet1, adip_expediteur);
        break;
    }
}

/* Fonction du thread serveur UDP */
void *serveur_udp(void *p)
{
    int                sock;
    struct sockaddr_in local, expediteur;
    socklen_t          taille_exp;
    char               buf[TAILLE_MAX_MSG];
    char               adip[INET_ADDRSTRLEN];
    int                opt = 1, nb;

    (void)p; /* paramètre non utilisé ici */

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { perror("socket serveur_udp"); return NULL; }

    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &opt, sizeof(opt));

    memset(&local, 0, sizeof(local));
    local.sin_family      = AF_INET;
    local.sin_port        = htons(PORT_BEUIP);
    local.sin_addr.s_addr = INADDR_ANY;

    if (bind(sock, (struct sockaddr *)&local, sizeof(local)) < 0) {
        perror("bind serveur_udp");
        close(sock);
        return NULL;
    }

    /* Annonce de présence sur toutes les interfaces */
    envoi_broadcast_interfaces(CODE_PRESENCE, "");

    printf("[BEUIP] Serveur UDP actif sur le port %d (pseudo: %s)\n",
           PORT_BEUIP, mon_pseudo);

    while (serveur_actif) {
        taille_exp = sizeof(expediteur);
        nb = recvfrom(sock, buf, sizeof(buf) - 1, 0,
                      (struct sockaddr *)&expediteur, &taille_exp);
        if (nb < 0) {
            if (errno == EINTR) continue;
            break;
        }
        buf[nb] = '\0';

        inet_ntop(AF_INET, &expediteur.sin_addr, adip, sizeof(adip));

        /* Ignorer nos propres messages */
        if (strcmp(adip, "127.0.0.1") == 0) continue;

        traite_datagramme(buf, adip);
    }

    close(sock);
    printf("[BEUIP] Serveur UDP arrêté.\n");
    return NULL;
}

/* ================================================================ */
/* Thread serveur TCP                                               */
/* ================================================================ */

/* Envoie le contenu d'un répertoire ou d'un fichier via fd */
void envoiContenu(int fd)
{
    char  octet;
    char  nom_fic[256];
    char  chemin[512];
    char *argv_ls[4]  = { "ls", "-l", NULL, NULL };
    char *argv_cat[3] = { "cat", NULL, NULL };
    int   n;
    pid_t pid;

    /* Lecture du premier octet de commande */
    if (read(fd, &octet, 1) <= 0) return;

    if (octet == TCP_LISTE) {
        /* Envoi de la liste des fichiers via ls */
        snprintf(chemin, sizeof(chemin), "%s/", reppub);
        argv_ls[2] = chemin;

        pid = fork();
        if (pid < 0) { perror("fork ls"); return; }
        if (pid == 0) {
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            execvp("ls", argv_ls);
            perror("exec ls");
            exit(1);
        }
        waitpid(pid, NULL, 0);

    } else if (octet == TCP_FICHIER) {
        /* Lecture du nom de fichier terminé par '\n' */
        n = 0;
        while (n < (int)(sizeof(nom_fic) - 1)) {
            if (read(fd, &nom_fic[n], 1) <= 0) break;
            if (nom_fic[n] == '\n') break;
            n++;
        }
        nom_fic[n] = '\0';

        /* Construction du chemin complet (sécurisation basique) */
        if (strchr(nom_fic, '/') || strncmp(nom_fic, "..", 2) == 0) {
            dprintf(fd, "Erreur: nom de fichier invalide.\n");
            return;
        }
        snprintf(chemin, sizeof(chemin), "%s/%s", reppub, nom_fic);

        if (access(chemin, R_OK) != 0) {
            dprintf(fd, "Erreur: fichier '%s' introuvable.\n", nom_fic);
            return;
        }

        argv_cat[1] = chemin;
        pid = fork();
        if (pid < 0) { perror("fork cat"); return; }
        if (pid == 0) {
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            execvp("cat", argv_cat);
            perror("exec cat");
            exit(1);
        }
        waitpid(pid, NULL, 0);

    } else {
        fprintf(stderr, "[TCP] Commande inconnue : '%c'\n", octet);
    }
}

/* Fonction du thread serveur TCP */
void *serveur_tcp(void *rep)
{
    int                sock, client;
    struct sockaddr_in local, distant;
    socklen_t          taille_dist;
    int                opt = 1;

    if (rep) strncpy(reppub, (char *)rep, sizeof(reppub) - 1);

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { perror("socket serveur_tcp"); return NULL; }

    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&local, 0, sizeof(local));
    local.sin_family      = AF_INET;
    local.sin_port        = htons(PORT_BEUIP);
    local.sin_addr.s_addr = INADDR_ANY;

    if (bind(sock, (struct sockaddr *)&local, sizeof(local)) < 0) {
        perror("bind serveur_tcp");
        close(sock);
        return NULL;
    }

    if (listen(sock, 5) < 0) {
        perror("listen serveur_tcp");
        close(sock);
        return NULL;
    }

    printf("[BEUIP] Serveur TCP actif sur le port %d (rép: %s)\n",
           PORT_BEUIP, reppub);

    while (serveur_actif) {
        taille_dist = sizeof(distant);
        client = accept(sock, (struct sockaddr *)&distant, &taille_dist);
        if (client < 0) {
            if (errno == EINTR || !serveur_actif) break;
            continue;
        }
#ifdef TRACE
        char adip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &distant.sin_addr, adip, sizeof(adip));
        printf("[TRACE TCP] connexion de %s\n", adip);
#endif
        envoiContenu(client);
        close(client);
    }

    close(sock);
    printf("[BEUIP] Serveur TCP arrêté.\n");
    return NULL;
}

/* ================================================================ */
/* Fonctions client TCP                                             */
/* ================================================================ */

/* Demande la liste des fichiers d'un pair */
void demandeListe(const char *pseudo)
{
    const char *adip;
    int         sock, nb;
    struct sockaddr_in dest;
    char        buf[TAILLE_MAX_MSG];
    char        octet = TCP_LISTE;

    adip = chercheAdresseParPseudo(pseudo);
    if (!adip) {
        printf("Utilisateur '%s' introuvable dans la liste.\n", pseudo);
        return;
    }

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { perror("socket demandeListe"); return; }

    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port   = htons(PORT_BEUIP);
    inet_pton(AF_INET, adip, &dest.sin_addr);

    if (connect(sock, (struct sockaddr *)&dest, sizeof(dest)) < 0) {
        perror("connect demandeListe");
        close(sock);
        return;
    }

    write(sock, &octet, 1);

    printf("--- Fichiers de %s (%s) ---\n", pseudo, adip);
    while ((nb = read(sock, buf, sizeof(buf) - 1)) > 0) {
        buf[nb] = '\0';
        fputs(buf, stdout);
    }
    printf("--- Fin ---\n");

    close(sock);
}

/* Télécharge un fichier depuis un pair */
void demandeFichier(const char *pseudo, const char *nomfic)
{
    const char *adip;
    int         sock, nb, fd_local;
    struct sockaddr_in dest;
    char        buf[TAILLE_MAX_MSG];
    char        chemin_local[512];
    char        requete[300];

    adip = chercheAdresseParPseudo(pseudo);
    if (!adip) {
        printf("Utilisateur '%s' introuvable dans la liste.\n", pseudo);
        return;
    }

    /* Sécurisation : pas de '/' ou '..' dans le nom */
    if (strchr(nomfic, '/') || strncmp(nomfic, "..", 2) == 0) {
        printf("Erreur: nom de fichier invalide.\n");
        return;
    }

    /* Vérification : le fichier local n'existe pas déjà */
    snprintf(chemin_local, sizeof(chemin_local), "%s/%s", reppub, nomfic);
    if (access(chemin_local, F_OK) == 0) {
        printf("Erreur: le fichier '%s' existe déjà localement.\n", nomfic);
        return;
    }

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { perror("socket demandeFichier"); return; }

    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port   = htons(PORT_BEUIP);
    inet_pton(AF_INET, adip, &dest.sin_addr);

    if (connect(sock, (struct sockaddr *)&dest, sizeof(dest)) < 0) {
        perror("connect demandeFichier");
        close(sock);
        return;
    }

    /* Envoi de la requête : 'F' + nom + '\n' */
    snprintf(requete, sizeof(requete), "F%s\n", nomfic);
    write(sock, requete, strlen(requete));

    /* Ouverture du fichier de destination */
    fd_local = open(chemin_local, O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (fd_local < 0) {
        perror("open fichier local");
        close(sock);
        return;
    }

    printf("Téléchargement de '%s' depuis %s...\n", nomfic, pseudo);
    while ((nb = read(sock, buf, sizeof(buf))) > 0) {
        if (write(fd_local, buf, nb) != nb) {
            perror("write fichier local");
            break;
        }
    }

    close(fd_local);
    close(sock);
    printf("Fichier '%s' téléchargé dans %s\n", nomfic, chemin_local);
}
