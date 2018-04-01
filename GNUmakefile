CLIENT_DIR=client
SERVER_DIR=server
BUILD_DIR=build
TIMEOUT=10
VALGRIND=valgrind --leak-check=yes --error-exitcode=1
CC=gcc
CFLAGS=-g -std=gnu99 -Wall -Wextra -pedantic -D_REENTRENT=1
ifdef BEG_PRO_ATOMIC
CFLAGS += -DBEG_PRO_ATOMIC
endif
LDFLAGS=-pthread

CLT_O = main.o client_thread.o
SRV_O = main.o server_thread.o banker.o

.PHONY: default all clean format client server test \
	run \
	run-client run-server \
	run-valgrind-client run-valgrind-server

default: all

all: $(BUILD_DIR)/tp2_client $(BUILD_DIR)/tp2_server rapport.pdf

$(BUILD_DIR)/tp2_client: $(patsubst %.o,$(BUILD_DIR)/client/%.o, $(CLT_O))
	$(CC) $(LDFLAGS) -o $@ $(patsubst %.o,$(BUILD_DIR)/client/%.o, $(CLT_O))

$(BUILD_DIR)/tp2_server: $(patsubst %.o,$(BUILD_DIR)/server/%.o, $(SRV_O))
	$(CC) $(LDFLAGS) -o $@ $(patsubst %.o,$(BUILD_DIR)/server/%.o, $(SRV_O))

$(BUILD_DIR)/%.o: %.c
	@[ -d "$$(dirname "$@")" ] || mkdir -p "$$(dirname "$@")"
	$(CC) $(CFLAGS) -c -o $@ $<



# Lancer le client et le serveur, utilisant le port 2018 pour communiquer.
# Ici on utilise par défaut 3 threads du côté du serveur et 5 du côté
# du client.  Chaque client envoie 50 requêtes.  Il a 5 types de resources
# à gérer avec quantités respectivement 10, 4, 23, 1, et 2.
run: all
	$(BUILD_DIR)/tp2_server 2018 3 & 		  \
	$(BUILD_DIR)/tp2_client 2018 1 50   10 4 23 1 2 & \
	wait

run-server: all
	@$(BUILD_DIR)/tp2_server 2018 3

run-client: all
	@$(BUILD_DIR)/tp2_client 2018 5 50   10 4 23 1 2

# Mis à un thread pour déboguer.
run-valgrind-server: all
	$(VALGRIND) $(BUILD_DIR)/tp2_server 2018 1

run-valgrind-client: all
	$(VALGRIND) $(BUILD_DIR)/tp2_client 2018 5 50   10 4 23 1 2

indent:
	indent -linux server/*.{c,h} client/*.{c,h}

clean:
	$(RM) -r $(BUILD_DIR) *.aux *.log */*~*

%.pdf: %.tex
	pdflatex -halt-on-error $<

release:
	tar -czv -f tp2.tar.gz --transform 's|^|tp2/|' \
	    */*.[ch] *.tex *.md GNUmakefile


$(BUILD_DIR)/server/main.o $(BUILD_DIR)/server/server_thread.o $(BUILD_DIR)/server/banker.o: \
    server/server_thread.h
$(BUILD_DIR)/client/main.o $(BUILD_DIR)/client/client_thread.o: \
    client/client_thread.h
