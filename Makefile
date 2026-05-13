PORT ?= 4242
CFLAGS = -DPORT=$(PORT) -g -Wall -Wextra

all: server client

server: server.o game.o socket.o protocol.o
	gcc ${CFLAGS} -o $@ $^

client: client.o game.o socket.o protocol.o
	gcc ${CFLAGS} -o $@ $^

server.o: server.c socket.h game.h protocol.h
	gcc ${CFLAGS} -c $<

client.o: client.c socket.h game.h protocol.h
	gcc ${CFLAGS} -c $<

socket.o: socket.c socket.h
	gcc ${CFLAGS} -c $<

game.o: game.c game.h
	gcc ${CFLAGS} -c $<

protocol.o: protocol.c protocol.h
	gcc ${CFLAGS} -c $<

clean:
	rm *.o server client

