# Makefile - biceps v3

# Cibles :
#   make              -> biceps (production)
#   make memory-leak  -> biceps-memory-leaks (débogage Valgrind)
#   make clean        -> suppression des fichiers générés

CC      = gcc
CFLAGS  = -Wall -Werror -std=c99 -D_GNU_SOURCE
LDFLAGS = -lpthread

# Fichiers sources communs
SRCS    = biceps.c creme.c gescom.c
OBJS    = $(SRCS:.c=.o)

# ---- Cible par défaut ----
all: biceps

biceps: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "==> biceps compilé avec succès."

# ---- Cible débogage mémoire (Valgrind) ----
OBJS_DBG = $(SRCS:.c=_dbg.o)

memory-leak: $(OBJS_DBG)
	$(CC) -g -O0 -std=c99 -D_GNU_SOURCE -o biceps-memory-leaks $^ $(LDFLAGS)
	@echo "==> biceps-memory-leaks compilé. Lancer avec :"
	@echo "    valgrind --leak-check=full --track-origins=yes ./biceps-memory-leaks"

%_dbg.o: %.c
	$(CC) -g -O0 -std=c99 -D_GNU_SOURCE -c $< -o $@

# ---- Cible avec traces ----
trace: CFLAGS += -DTRACE
trace: biceps
	@echo "==> biceps compilé avec TRACE activé."

trace2: CFLAGS += -DTRACE -DTRACE2
trace2: biceps
	@echo "==> biceps compilé avec TRACE + TRACE2 activés."

# ---- Règle de compilation générique ----
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# ---- Nettoyage ----
clean:
	rm -f $(OBJS) $(OBJS_DBG) biceps biceps-memory-leaks
	@echo "==> Nettoyage effectué."

# ---- Déclaration des cibles non-fichiers ----
.PHONY: all memory-leak trace trace2 clean
