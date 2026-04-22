
# SABIR Mohammed El Mahdi & FILAHI Amine  
# biceps v3 — Bel Interpréteur de Commandes des Élèves de Polytech Sorbonne




---

## Table des matières

1. [Structure du code](#structure-du-code)
2. [Compilation](#compilation)
3. [Utilisation](#utilisation)
4. [Architecture multi-threadée](#architecture-multi-threadée)
5. [Protocole BEUIP](#protocole-beuip)
6. [Vérification des fuites mémoire (Valgrind)](#vérification-valgrind)
7. [Options de compilation](#options-de-compilation)

---

## Structure du code

```
biceps_v3/
├── biceps.c      — Shell principal : boucle REPL, dispatch commandes
├── creme.c       — Protocole BEUIP : réseau UDP/TCP, liste chaînée
├── creme.h       — Entêtes, structures, constantes réseau
├── gescom.c      — Gestionnaire des commandes internes beuip
├── gescom.h      — Prototypes des fonctions de gescom
├── Makefile      — Compilation production et débogage
└── README.md     — Ce fichier
```

### Rôle de chaque module

| Fichier | Responsabilité |
|---------|---------------|
| `biceps.c` | Boucle principale du shell, lecture des lignes, dispatch vers internes ou `execvp` |
| `creme.c/h` | Toute la couche réseau : sockets UDP/TCP, threads serveurs, liste chaînée des contacts, fonctions client TCP |
| `gescom.c/h` | Interprétation et exécution des sous-commandes `beuip start/stop/list/message/ls/get`, parsing de la ligne |

---

## Compilation

```bash
# Compilation standard (production)
make

# Compilation pour Valgrind (symboles de débogage, sans optimisation)
make memory-leak

# Compilation avec traces d'exécution niveau 1
make trace

# Compilation avec traces niveaux 1 et 2
make trace2

# Nettoyage des fichiers générés
make clean
```

L'exécutable produit par `make` est **`biceps`**.
L'exécutable produit par `make memory-leak` est **`biceps-memory-leaks`**.

---

## Utilisation

Lancer le shell :

```bash
./biceps
```

### Commandes internes disponibles

```
beuip start <pseudo> [reppub]    Démarrer les serveurs UDP et TCP
                                  reppub : répertoire des fichiers partagés (défaut : pub/)

beuip stop                       Arrêter proprement les serveurs (envoie CODE_DEPART à tous)

beuip list                       Afficher la liste des utilisateurs connectés
                                  Format : 192.168.88.151 : nassim

beuip message <pseudo> <msg>     Envoyer un message à un utilisateur
beuip message all <msg>          Envoyer un message à tous les utilisateurs

beuip ls <pseudo>                Lister les fichiers du répertoire public d'un pair (TCP)

beuip get <pseudo> <fichier>     Télécharger un fichier depuis le répertoire public d'un pair

aide / help                      Afficher l'aide
exit / quit                      Quitter biceps (arrête les serveurs si actifs)
<commande système>               Exécuter n'importe quelle commande Unix via fork/exec
```

### Exemple de session

```
biceps> beuip start alice pub
[BEUIP] Répertoire 'pub' créé.
[BEUIP] Serveur UDP actif sur le port 9998 (pseudo: alice)
[BEUIP] Serveur TCP actif sur le port 9998 (rép: pub)
[BEUIP] Serveurs démarrés avec le pseudo 'alice'.

biceps> beuip list
192.168.88.152 : bob
192.168.88.153 : charlie

biceps> beuip message bob Salut Bob !

biceps> beuip ls bob
--- Fichiers de bob (192.168.88.152) ---
total 8
-rw-r--r-- 1 bob bob 1234 mars 19 notes.txt
--- Fin ---

biceps> beuip get bob notes.txt
Téléchargement de 'notes.txt' depuis bob...
Fichier 'notes.txt' téléchargé dans pub/notes.txt

biceps> beuip stop
[BEUIP] Serveurs arrêtés.

biceps> exit
Au revoir !
```

---

## Architecture multi-threadée

### Motivation

La version 2 de biceps utilisait `fork()` pour créer un processus fils qui gérait le serveur UDP. Cette approche présentait deux problèmes majeurs :

1. **Isolation mémoire** : le processus fils ne peut pas partager la table des contacts avec le shell. Il fallait s'envoyer des messages à soi-même pour communiquer, ce qui ouvre une faille **man-in-the-middle** (un attaquant peut usurper l'adresse `127.0.0.1`).
2. **Sécurité** : les codes `'3'`, `'4'` et `'5'` du protocole permettaient au serveur UDP de traiter des demandes de liste et d'envoi de messages — fonctions qui deviennent inutilement exposées au réseau.

### Solution : POSIX threads (`pthreads`)

```
Processus biceps
│
├── Thread principal (main)
│     └─ Boucle shell : saisie, parse, dispatch
│           ├─ Lecture/affichage de liste_contacts (avec mutex)
│           └─ Envoi UDP unicast via envoi_udp_unicast()
│
├── Thread serveur_udp
│     └─ recvfrom() en boucle
│           └─ ajouteElt() / supprimeElt() sur liste_contacts (avec mutex)
│
└── Thread serveur_tcp
      └─ accept() en boucle
            └─ envoiContenu() → fork + exec ls/cat avec dup2()
```

### Protection des données partagées

La liste chaînée `liste_contacts` est accédée en **écriture** par le thread `serveur_udp` et en **lecture** par le thread principal. Un `pthread_mutex_t` nommé `mutex_liste` protège tous les accès :

```c
pthread_mutex_lock(&mutex_liste);
/* accès à liste_contacts */
pthread_mutex_unlock(&mutex_liste);
```

### Arrêt propre des threads

Lors de `beuip stop` :

1. Envoi d'un `CODE_DEPART ('0')` en unicast à chaque contact de la liste.
2. Mise à zéro du flag `serveur_actif`.
3. `pthread_cancel()` sur chaque thread (les appels bloquants `recvfrom`/`accept` sont des points d'annulation POSIX).
4. `pthread_join()` pour attendre la fin effective.
5. `videeListe()` libère la mémoire allouée.

### Sécurisation du protocole

Depuis la version 3, les codes `'3'` (liste), `'4'` (message ciblé) et `'5'` (message broadcast) ne sont **plus traités** par le serveur UDP. Toute réception de ces codes génère un message d'alerte sur `stderr` :

```
[SECURITE] Tentative suspecte : octet1='3' de 192.168.88.200
```

---

## Protocole BEUIP

| octet1 | Signification | Traitement |
|--------|--------------|------------|
| `'0'` | Départ (sans AR) | Suppression du contact |
| `'1'` | Présence (broadcast) | Ajout du contact + réponse unicast |
| `'2'` | Accusé de réception | Ajout du contact |
| `'3'` | Liste (obsolète) | Rejeté — alerte sécurité |
| `'4'` | Message ciblé (obsolète) | Rejeté — alerte sécurité |
| `'5'` | Message tous (obsolète) | Rejeté — alerte sécurité |
| `'9'` | Message unicast | Affichage à l'écran |

Format du datagramme : `octet1:pseudo:message\0`

---

## Vérification Valgrind

```bash
# Compilation en mode débogage
make memory-leak

# Lancer avec Valgrind
valgrind --leak-check=full \
         --track-origins=yes \
         --show-leak-kinds=all \
         ./biceps-memory-leaks
```

Points de vigilance implémentés :

- `free()` systématique dans `supprimeElt()` et `videeListe()`.
- Les sockets UDP/TCP sont fermés dans tous les chemins d'exécution (succès et erreur).
- Les descripteurs de fichiers TCP client sont fermés après chaque `envoiContenu()`.
- Les processus fils (ls, cat) sont attendus via `waitpid()` pour éviter les zombies.

---

## Options de compilation

| Option | Effet |
|--------|-------|
| `-DTRACE` | Active les messages de trace niveau 1 (connexions, entrées/sorties de liste) |
| `-DTRACE2` | Active les traces niveau 2 (retours de commandes shell) — nécessite `-DTRACE` |

Exemple de compilation manuelle avec trace :

```bash
gcc -Wall -Werror -std=c99 -D_GNU_SOURCE -DTRACE -DTRACE2 \
    biceps.c creme.c gescom.c -o biceps -lpthread
```

---

*Fin du README — biceps v3, mars 2026*
