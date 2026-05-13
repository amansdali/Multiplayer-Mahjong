#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>

#include "socket.h"
#include "game.h"
#include "protocol.h"

#ifndef PORT
  #define PORT 38558
#endif

#define BUFSIZE 512
#define MAX_CLIENTS 64

struct client {
    int fd;
    char buf[BUFSIZE];
    int buf_len;
    struct in_addr ipaddr;
    struct client *next;
    Player *player;
};

// a queue of Games, games at the front will be populated with players first
typedef struct game_node {
    Game *game;
    struct game_node * next;
} GameNode;

static struct client *addclient(struct client *top, int fd, struct in_addr addr);
static struct client *removeclient(struct client *top, int fd);
static GameNode *get_game_node(GameNode *front, Game *game);
int handleclient(struct client *p);
int bindandlisten(void);
static GameNode *add_gamenode(GameNode *back);
static GameNode *remove_gamenode(GameNode **front, GameNode *node, GameNode **back);
static GameNode *get_available_game(GameNode *front);
int add_player_to_game(struct client *top, GameNode **front, GameNode **back);
int handle_msg(Msg *msg, Player *player);
int start_game(Game *game);
int draw_phase(Game *game);
int play_phase(Game *game);
int respond_phase(Game *game, int last_player_turn, char category, char value);
struct client *delete_game(struct client *top, Game *game);
bool win(Player *player);
int end_game(Game *game, int player_number);
int make_announcement(Game *game, char action, int player_number, char *details);
int handle_steal_tile(Game *game, int player_index);

/*
 * Search the first n characters of buf for a network newline (\r\n).
 * Return one plus the index of the '\n' of the first network newline,
 * or -1 if no network newline is found. The return value is the index into buf
 * where the current line ends.
 */
int find_network_newline(const char *buf, int n) {
    for(int i = 0; i<n-1; ++i){
        if (buf[i]=='\r' && buf[i+1]=='\n'){
            return i+2;
        }
    }
    return -1;
}

/*
 * main function. sets up intial variables and runs the main loop.
 * uses select to watch for new clients and messages from clients.
 */
int main() {

    srand(time(NULL));

    int clientfd, maxfd, nready;
    struct client *client_p;
    struct client *client_head = NULL;
    GameNode *game_head = NULL;
    GameNode *game_tail = NULL;
    socklen_t len;
    struct sockaddr_in q;
    struct timeval tv;
    fd_set allset;
    fd_set rset;
    
    int i;

    int listenfd = bindandlisten(); // sets up a listening socket

    FD_ZERO(&allset);
    FD_SET(listenfd, &allset);
    maxfd = listenfd;

    bool running = true;
    while (running) {
	rset = allset; // Reset rset to allset before each call to select

        // Similar to rset, timeout is also modified by select,
        // so we reset it each time before calling select.
        tv.tv_sec = 10;
        tv.tv_usec = 0;

        nready = select(maxfd + 1, &rset, NULL, NULL, &tv);
        if (nready == 0) {
            continue;
        }

        if (nready == -1) {
            perror("select");
            continue; // continuing might be okay since select can return -1 if interrupted by a signal
        }

        /*
         * select returns the number of file descriptors that are ready for I/O,
         * but to know which ones are ready, we need to check rset by using FD_ISSET
         * on each file descriptor we are interested in.
         *
         * If FD_ISSET returns true, then we can do what we want with that file descriptor.
         */
        if (FD_ISSET(listenfd, &rset)){ // FD_ISSET returns true if listenfd is ready
            printf("a new client is connecting\n");
            len = sizeof(q);
            if ((clientfd = accept(listenfd, (struct sockaddr *)&q, &len)) < 0) {
                perror("accept");
                exit(1);
            }

            // Add the new client to the set of file descriptors to monitor
            // and update maxfd if necessary. We MUST use this if-statement
            // because clientfd could be any valid file descriptor, not just maxfd + 1,
            // since we close file descriptors as clients disconnect.
            FD_SET(clientfd, &allset);
            if (clientfd > maxfd) {
                maxfd = clientfd;
            }
            printf("connection from %s\n", inet_ntoa(q.sin_addr));
            client_head = addclient(client_head, clientfd, q.sin_addr);

	    // assign the new client to a game
	    if (add_player_to_game(client_head, &game_head, &game_tail) < 0) {
	        fprintf(stderr,"error adding player to a game for client %s\n", inet_ntoa(q.sin_addr));
	    } else { // send a message to all members of that game regarding the current join-game status
		Game *client_game = client_head->player->game;
		bool can_continue = true;
		for (i=0; can_continue&&i<client_game->num_players; i++) {
	            // Create a msg struct
                    Msg *msg;
                    if((msg = malloc(sizeof(Msg))) == NULL) {
                        perror("malloc");
                        exit(1);
                    }
                    msg->msg_type = JOINING_GAME;

		    JoinPayload *jpayload;
		    if ((jpayload = malloc(sizeof(JoinPayload))) == NULL) { // create a JoinPayload
		        perror("malloc");
			free(msg);
			exit(1);
		    }
		    jpayload->player_number = client_game->players[i].player_number;
		    jpayload->players_found = client_game->num_players;
		    parseJoinPayload(jpayload, msg->payload, 0);

		    char buf[BUFSIZE];
		    if(encode(msg, buf, BUFSIZE) == 0) {
			// write msg, if write returns -1, remove the client and send an ending message too 
			// all players in that client's game before removing them as well. Otherwise if write returns
			// a number less than J_MSG_SIZE, keep writing until all bytes are written.
                        size_t msg_len = J_MSG_SIZE;
                        size_t total_written = 0;
                        ssize_t nbytes=0;
                        const char *ptr = buf;
			int soc = client_game->players[i].socket;
                        while(total_written<msg_len){
                            nbytes = write(soc, ptr+total_written, msg_len-total_written);
                            if(nbytes<0){
                                perror("write");
                                printf("Connection to client lost\n");
				can_continue = false;
				int tmp_fds[4] = {0};
                                int num_tmp_fds = 0;
				Game *game_ptr = NULL;
                                GameNode *gamenode_ptr = NULL;
                                if(client_p->player != NULL){
				    game_ptr = client_game;
                                    gamenode_ptr = get_game_node(game_head, game_ptr);
                                    num_tmp_fds = game_ptr->num_players;
                                    for(int player_num = 0; player_num<num_tmp_fds; player_num++){
                                        tmp_fds[player_num] = game_ptr->players[player_num].socket;
                                    }
                                }else{
                                    printf("tried to access a client with no player\n");
                                }
                                if (game_ptr != NULL){
                                    client_head = delete_game(client_head, game_ptr);
                                }
                                if (gamenode_ptr != NULL) {
                                    game_head = remove_gamenode(&game_head, gamenode_ptr, &game_tail);
                                }
                                for(int player_num = 0; player_num<num_tmp_fds; player_num++){
                                    FD_CLR(tmp_fds[player_num], &allset); // Clear the file descriptor from allset
                                }
                                // re-find the maxfd
                                maxfd = STDIN_FILENO;
                                struct client *c = client_head;
                                while(c!=NULL){
                                    if(c->fd > maxfd){
                                        maxfd = c->fd;
                                    }
                                    c = c->next;
                                }
                                if(listenfd > maxfd){
                                    maxfd = listenfd;
                                }
				break;
                            }
                            total_written = total_written+nbytes;
                        }
		    } else {
			fprintf(stderr,"error sending joining message to client");
		    }

		    free(msg);
		    free(jpayload);
		}

		// if game has enough players, start the game
		if(can_continue && client_game->game_status == READY_TO_START) {
		    start_game(client_game);
		}
	    }
        }

        // Check the client file descriptors to see if they are ready for reading
        for(i = 0; i <= maxfd; i++) {
            if (FD_ISSET(i, &rset)) {
                for (client_p = client_head; client_p != NULL; client_p = client_p->next) {
                    if (client_p->fd == i) {

			// call handleclient helper function, which returns 0 on success, -1 on failure, and -2 if the client sends a leaave message
                        int result = handleclient(client_p);

                        /* If result is < 0 then the client has closed or an error occured with the client, or the client left the game.
			 * Remove the client, along with the game they were a part of and any other players in that game. 
			 */
                        if (result < 0) {
			    if(result == -1){
			        printf("an issue occurred with a client. Removing client and their corresponding game. \n");
			    }else if(result == -2){ // if result is -2, the client chose to leave
				printf("a client sent a leave message, removing client and their corresponding game\n");
			    }
			    int tmp_fds[4] = {0};
			    int num_tmp_fds = 0;
			    Game *game_ptr = NULL;
			    GameNode *gamenode_ptr = NULL;
			    if(client_p->player != NULL){
				game_ptr = client_p->player->game;
                                gamenode_ptr = get_game_node(game_head, game_ptr);
				num_tmp_fds = game_ptr->num_players;
				for(int player_num = 0; player_num<num_tmp_fds; player_num++){
                                    tmp_fds[player_num] = game_ptr->players[player_num].socket; // file descriptors of the players in that game
				}
			    }else{
				printf("tried to access a client with no player\n");
			    }
			    if (game_ptr != NULL){
			        client_head = delete_game(client_head, game_ptr); // delete the game, including the players/clients in that game
			    } 
			    if (gamenode_ptr != NULL) {
			        game_head = remove_gamenode(&game_head, gamenode_ptr, &game_tail); // remove the game node with that game
			    }
			    for(int player_num = 0; player_num<num_tmp_fds; player_num++){
			        FD_CLR(tmp_fds[player_num], &allset); // Clear the file descriptor from allset
			    }
			    // re-find the maxfd
                            maxfd = STDIN_FILENO;
		            struct client *c = client_head; 
			    while(c!=NULL){
			        if(c->fd > maxfd){
                                    maxfd = c->fd;
				}
				c = c->next;
                            }
			    if(listenfd > maxfd){
			        maxfd = listenfd;
			    }
                        }
                        break;
                    }
                }
            }
        }

    }

    close(listenfd);
    return 0;
}

/*
 * Use the buffer associated with a client to handle input from a client. 
 * Checks for network_newlines (\r\n), and handles each msg found.
 * Only performs one read, which is guaranteed to read something due to the use of select in the main loop, unless client disconnected.
 * Return 0 on success, -1 if client disconnected or the buffer is full (an error occurred in the communication between the server and client)
 */
int handleclient(struct client *p) {
    int room = BUFSIZE - p->buf_len;  // How many bytes remaining in buffer?
    char *after = p->buf + p->buf_len;       // Pointer to position after the data in buf

    int nbytes = read(p->fd, after, room);
    if (nbytes > 0){
	// update buf_len
        p->buf_len = p->buf_len + nbytes;

        int where;

        // The loop condition below calls find_network_newline
        // to determine if a full line has been read from the client.
        // Note: we use a loop here because a single read might result in
        // more than one full line.
        while ((where = find_network_newline(p->buf, p->buf_len)) > 0) {
            // where is now the index into buf immediately after
            // the first network newline
            p->buf[where-2]='\0';

	    // handle the message
            printf("Recieved message: %s\n", p->buf);
	    Msg *msg = malloc(sizeof(Msg));
	    if(msg==NULL){
	        perror("malloc");
		exit(1);
	    }
	    decode(msg, p->buf);
            int success = handle_msg(msg, p->player);
	    free(msg);
	    if(success<0){
	        return success;
	    }
            // Update buf_len and remove the full line from the buffer
            p->buf_len = p->buf_len - where;
            memmove(p->buf, p->buf+where, p->buf_len);
        }
	room = BUFSIZE - p->buf_len;
	if (room == 0) {
            fprintf(stderr, "Client %d sent malformed message\n", p->fd);
            return -1; // buffer full
        }
	return 0;
    } else if (nbytes == 0){
        // client disconnected
	return -1;
    } else {
        perror("read");
	return -1;
    }
}

/* 
 * bind and listen, abort on error
 * returns FD of listening socket
 */
int bindandlisten(void) {
    // Initialize a struct containing the address of this server.
    struct sockaddr_in *self = init_server_addr(PORT);
    int listenfd = set_up_server_socket(self, 1);
    free(self);
    return listenfd;
}

/*
 * add a client to the list, return a pointer to the client that was added
 */
static struct client *addclient(struct client *top, int fd, struct in_addr addr) {
    struct client *p = malloc(sizeof(struct client));
    if (!p) {
        perror("malloc");
        exit(1);
    }

    printf("Adding client %s\n", inet_ntoa(addr));

    p->fd = fd;
    p->buf_len = 0;
    p->buf[0] = '\0';
    p->ipaddr = addr;
    p->next = top;
    p->player = NULL;
    top = p;
    return top;
}

/*
 * removes a client, marks the player's socket as -1
 */
static struct client *removeclient(struct client *top, int fd) {
    struct client **p;

    for (p = &top; *p && (*p)->fd != fd; p = &(*p)->next)
       ;
    if (*p) {
        struct client *t = (*p)->next;
        printf("Removing client %d %s\n", fd, inet_ntoa((*p)->ipaddr));

	if((*p)->player != NULL){
            (*p)->player->socket = -1;
	}
	close((*p)->fd);
        free(*p);
        *p = t;
    } else {
        fprintf(stderr, "Trying to remove fd %d, but I don't know about it\n", fd);
    }
    return top;
}


/*
 * Find and return the client with a given fd. Return NULL if not found.
 */

static GameNode *get_game_node(GameNode *front, Game *game){
    GameNode *curr = front;
    while (curr != NULL) {
        if (curr->game == game) {
            return curr;
	}
	curr = curr->next;
    }
    return curr;

}

static GameNode *add_gamenode(GameNode *back) {
    GameNode *p = malloc(sizeof(GameNode));
    if (!p) {
        perror("malloc");
        exit(1);
    }

    p->game = malloc(sizeof(Game));
    if (!p->game) {
        perror("malloc");
	exit(1);
    }

    init_game(p->game);
    printf("Adding new game\n");

    p->next = NULL;

    if (back != NULL) {
    	back->next = p;
    }
    return p;
}

// remove a gamenode, assuming the game pointed to by the game node has already been deleted
static GameNode *remove_gamenode(GameNode **front, GameNode *node, GameNode **back) {
    GameNode **p;
 
    for (p = front; *p && *p != node; p = &(*p)->next)
       ;
    // Now, p points to (1) top, or (2) a pointer to another node
    // This avoids a special case for removing the head of the list
    if (*p) {
        GameNode *t = (*p)->next;
        free(*p);
        *p = t;

	// Deal with special case of removing tail
	if (t == NULL) {
            if (*front == NULL) { // list is empty
                *back = NULL;
            } else { // otherwise traverse the list to make back the last node
                GameNode *last = *front;
                while (last->next != NULL)
                    last = last->next;
                *back = last;
            }
        }
    } else {
        fprintf(stderr, "Trying to remove a game, but I don't know about it\n");
    }
    return *front;
}

static GameNode *get_available_game(GameNode *front) {
    GameNode *curr = front;
    while (curr != NULL) {
        if (curr->game->game_status == WAITING) {
            return curr;
	}
	curr = curr->next;
    }
    return NULL;
}

/*
 * Assigns to the client a player in a game. If no game available, creates a game. 
 */
int add_player_to_game(struct client *top, GameNode **front, GameNode **back) {
    GameNode *node = get_available_game(*front);
    if (node != NULL) {
        Player *player = add_player(node->game, top->fd);
	if (player == NULL) {
	    return -1;
	} else {
	    top->player = player;
	    return 0;
	}
    } else {
        node = add_gamenode(*back);
	*back = node; 
	if (*front == NULL) {
	    *front = node;
	}
        Player *player = add_player(node->game, top->fd);
	if (player == NULL) {
            return -1;
        } else {
            top->player = player;
            return 0;
        }
    }
}

/*
 * Handles a full message from a player
 */
int handle_msg(Msg *msg, Player *player){
    printf("Message recieved from a client. Message Type: %c. Message Payload: %s\n", msg->msg_type, msg->payload);

    switch(msg->msg_type){
        case DRAW_PHASE_ACTION:
	    int player_number = player->player_number;
	    Game *game = player->game;
	    if(player->game->curr_player == player->player_number-1){
	        if(game->curr_tile+1>=TILE_NUM){ // no more tiles to draw
	            if(end_game(game, 0)<0){
		        return -1;
		    }
	        } else{
		    game->curr_tile = game->curr_tile+1;
		    for (int i = 0; i<MAX_HAND_SIZE; i++){
		        if (player->hand[i].category == '\0' && player->hand[i].value == '\0'){
			    player->hand[i] = game->tiles[game->curr_tile];
			    if (i+1 < MAX_HAND_SIZE){
			        player->hand[i+1].category = '\0';
                                player->hand[i+1].value = '\0';
			    }
			    break;
		        }
		    }
		    char details[3];
		    details[0] = game->tiles[game->curr_tile].category;
		    details[1] = game->tiles[game->curr_tile].value;
	       	    details[2] = '\0';
		    
	            if(make_announcement(game, DREW_TILE, player_number, details)<0){
		        return -1;
		    }
	            if(win(player)){
		        if(end_game(player->game, player->player_number)<0){
			    return -1;
			}
	                break;
	            }
	            if(play_phase(player->game)<0){
		        return -1;
		    }	
	        }
	    } else {
	        if(make_announcement(game, ERROR, player_number, "Not your turn to draw tile")<0){
		    return -1;
		}
	    }
	    break;
	case PLAY_PHASE_ACTION:
	    ClientPlayPayload *ppayload = malloc(sizeof(ClientPlayPayload));
	    if(ppayload==NULL){
	        perror("malloc");
		exit(1);
	    }
	    parseClientPlayPayload(ppayload, msg->payload, 1);

	    if(player->game->curr_player == player->player_number-1){
	        bool tile_found = false;
		for (int i = 0; i<MAX_HAND_SIZE && player->hand[i].category != '\0' && player->hand[i].value != '\0'; i++){
                    if (player->hand[i].category == ppayload->tile.category && player->hand[i].value == ppayload->tile.value){
			player->game->discard[player->game->discard_count] = player->hand[i];
			player->game->discard_count = player->game->discard_count +1;

			for (int j = i; j<MAX_HAND_SIZE-1; j++){
			    player->hand[j] = player->hand[j+1];
			}
                        player->hand[MAX_HAND_SIZE-1].category = '\0';
                        player->hand[MAX_HAND_SIZE-1].value = '\0';
			tile_found = true;
                        break;
                    }
                }
		if (tile_found) {
	            char details[3];
                    details[0] = player->game->discard[player->game->discard_count-1].category;
                    details[1] = player->game->discard[player->game->discard_count-1].value;
                    details[2] = '\0';	
		    if(make_announcement(player->game, PLAYED_TILE, player->player_number, details)<0){
		        return -1;
		    }
	            if(respond_phase(player->game, player->game->curr_player, ppayload->tile.category, ppayload->tile.value)<0){
		        return -1;
		    }
		} else{
		    if(make_announcement(player->game, ERROR, player->player_number, "tile not found")<0){
			return -1;
		    }
		}
	    } else{
	         if(make_announcement(player->game, ERROR, player->player_number, "not your turn to play card")<0){
	             return -1;
		 }
	    }
	    free(ppayload);
            break;
	case RESPOND_PHASE_ACTION:
	    ClientRespondPayload *rpayload = malloc(sizeof(ClientRespondPayload));
            if(rpayload==NULL){
	        perror("malloc");
		exit(1);
	    }
	    parseClientRespondPayload(rpayload, msg->payload, 1);

	    if(player->game->curr_player != player->player_number-1){ // the player who just went shouldn't make a response
	    // first populate the response array
	        player->game->responses[player->player_number-1] = rpayload->response;
		if(rpayload->response == 's'){
                    player->game->tile_responses1[player->player_number-1] = rpayload->tiles[0];
		    player->game->tile_responses2[player->player_number-1] = rpayload->tiles[1];
		}
	        int total_responses = 0;
	        for(int i = 0; i<PLAYER_NUM; i++){
                    if(player->game->responses[i] != '\0'){
		        total_responses++;
		    }
                }
	        if(total_responses == PLAYER_NUM-1){
                    int first_steal_index = (player->game->curr_player + 1) % PLAYER_NUM;
		    while(first_steal_index != player->game->curr_player 
				    && player->game->responses[first_steal_index] != 's'){
		        first_steal_index = (first_steal_index + 1) % PLAYER_NUM;
		    }
		    if (player->game->responses[first_steal_index] == 's'){
		        // handle steal tile logic for player at first_steal_index
			Tile to_steal = player->game->discard[player->game->discard_count-1];
                        int success = handle_steal_tile(player->game, first_steal_index);
			if (success < 0){
		            if(make_announcement(player->game, ERROR, first_steal_index+1, "steal unsuccessful")<0){
				 return -1;
			    }
			}else{
			    char details[7];
                            details[0] = player->game->tile_responses1[first_steal_index].category;
                            details[1] = player->game->tile_responses1[first_steal_index].value;
                            details[2] = player->game->tile_responses2[first_steal_index].category;
                            details[3] = player->game->tile_responses2[first_steal_index].value;
			    details[4] = to_steal.category;
			    details[5] = to_steal.value;
			    details[6] = '\0';

                            if(make_announcement(player->game, STOLE_TILE, first_steal_index+1, details)<0){
				return -1;
			    }
			    
			    if(win(&(player->game->players[first_steal_index]))){
                                if(end_game(player->game, first_steal_index)<0){
		                    return -1;
				}
                                break;
                            }
			    
			    player->game->curr_player = first_steal_index;
			    if(play_phase(player->game)<0){
		                return -1;
			    }
			}
		    } else { // all pass
 			// clear the responses list to prepare for the next round
                        for(int i = 0; i<PLAYER_NUM; i++){
                            player->game->responses[i] = '\0';
                        }

	                if(make_announcement(player->game, PASSED, 1, "")<0){
		            return -1;
			}
			player->game->curr_player = (player->game->curr_player + 1) % PLAYER_NUM;
			if(draw_phase(player->game)<0){
			    return -1;
			}
		    }
	        }
	    } else{ 
                if(make_announcement(player->game, ERROR, player->player_number, "not your turn to respond")<0){
		    return -1;
		}
	    }

	    free(rpayload);
            break;
	case LEAVE:
	    return -2;
            break;
	default:
	    break;
    }
    return 0;
}

/*
 * Starts a game
 */
int start_game(Game *game) {
    game->game_status = IN_GAME;
    
    // Create a msg struct
    Msg *msg;
    if((msg = malloc(sizeof(Msg))) == NULL) {
        perror("malloc");
        exit(1);
    }
    msg->msg_type = STARTING_GAME;

    // Create a payload
    StartPayload *payload;
    if ((payload = malloc(sizeof(StartPayload))) == NULL) {
        perror("malloc");
        exit(1);
    }
    
    // modify the payload and encode/write the message to the client
    payload->player_turn = game->curr_player + 1;
    for (int i = 0; i<game->num_players; i++) {
	Player player = game->players[i];
	for (int i = 0; i<STANDARD_HAND_SIZE; i++) {
	    payload->hand[i] = player.hand[i];
	}
        parseStartPayload(payload, msg->payload, 0);

        char buf[BUFSIZE];
        if(encode(msg, buf, BUFSIZE) == 0) {

	    // write msg
            size_t msg_len = S_MSG_SIZE;
            size_t total_written = 0;
            ssize_t nbytes=0;
            const char *ptr =buf;
            while(total_written<msg_len){
                nbytes = write(player.socket, ptr+total_written, msg_len-total_written);
                if(nbytes<0){
                    perror("write");
                    printf("Connection to client lost\n");
                    free(msg);
                    free(payload);
                    return -1;
                }
                total_written = total_written+nbytes;
            }
        } else {
            fprintf(stderr,"error sending starting message to client");
        }
    }
    free(msg);
    free(payload);
    if(draw_phase(game)<0){
        return -1;
    }
    return 0;
}

int draw_phase(Game *game) {
    // Create a msg struct
    Msg *msg;
    if((msg = malloc(sizeof(Msg))) == NULL) {
        perror("malloc");
        exit(1);
    }
    msg->msg_type = DRAW_PHASE;

    // Create a payload
    DrawPlayPayload *payload;
    if ((payload = malloc(sizeof(DrawPlayPayload))) == NULL) {
        perror("malloc");
        exit(1);
    }

    // modify the payload and encode/write the message to the client
    payload->player_turn = game->curr_player + 1;
    parseDrawPlayPayload(payload, msg->payload, 0);
    char buf[BUFSIZE];
    encode(msg, buf, BUFSIZE);
    for (int i = 0; i<game->num_players; i++) {
        Player player = game->players[i];
        // write msg
        size_t msg_len = D_MSG_SIZE;
        size_t total_written = 0;
        ssize_t nbytes=0;
        const char *ptr =buf;
        while(total_written<msg_len){
            nbytes = write(player.socket, ptr+total_written, msg_len-total_written);
            if(nbytes<0){
                perror("write");
                printf("Connection to client lost\n");
                free(msg);
                free(payload);
                return -1;
            }
            total_written = total_written+nbytes;
        }
    }
    free(msg);
    free(payload);
    return 0;
}

int play_phase(Game *game) {
    // Create a msg struct
    Msg *msg;
    if((msg = malloc(sizeof(Msg))) == NULL) {
        perror("malloc");
        exit(1);
    }
    msg->msg_type = PLAY_PHASE;

    // Create a payload
    DrawPlayPayload *payload;
    if ((payload = malloc(sizeof(DrawPlayPayload))) == NULL) {
        perror("malloc");
        exit(1);
    }

    // modify the payload and encode/write the message to the client
    payload->player_turn = game->curr_player + 1;
    parseDrawPlayPayload(payload, msg->payload, 0);
    char buf[BUFSIZE];
    encode(msg, buf, BUFSIZE);
    for (int i = 0; i<game->num_players; i++) {
        Player player = game->players[i];
        
	// write msg
        size_t msg_len = P_MSG_SIZE;
        size_t total_written = 0;
        ssize_t nbytes=0;         
        const char *ptr =buf;
        while(total_written<msg_len){
            nbytes = write(player.socket, ptr+total_written, msg_len-total_written);
            if(nbytes<0){
                perror("write");
                printf("Connection to client lost\n");
                free(msg);
                free(payload);
                return -1;
            }
            total_written = total_written+nbytes;
        }
    }
    free(msg);
    free(payload);
    return 0;
}

int respond_phase(Game *game, int last_player_turn, char category, char value) {
    // Create a msg struct
    Msg *msg;
    if((msg = malloc(sizeof(Msg))) == NULL) {
        perror("malloc");
        exit(1);
    }
    msg->msg_type = RESPOND_PHASE;

    // Create a payload
    RespondPayload *payload;
    if ((payload = malloc(sizeof(RespondPayload))) == NULL) {
        perror("malloc");
        exit(1);
    }

    // modify the payload and encode/write the message to the client
    payload->last_player_turn = last_player_turn;
    payload->tile.category = category;
    payload->tile.value = value;
    parseRespondPayload(payload, msg->payload, 0);
    char buf[BUFSIZE];
    encode(msg, buf, BUFSIZE);
    for (int i = 0; i<game->num_players; i++) {
        Player player = game->players[i];
        
	// write msg
        size_t msg_len = R_MSG_SIZE;
        size_t total_written = 0;
        ssize_t nbytes=0;         
        const char *ptr =buf;
        while(total_written<msg_len){
            nbytes = write(player.socket, ptr+total_written, msg_len-total_written);
            if(nbytes<0){
                perror("write");
                printf("Connection to client lost\n");
                free(msg);
                free(payload);
                return -1;
            }
            total_written = total_written+nbytes;
        }
    }
    free(msg);
    free(payload);
    return 0;
}

/*
 * Delete a game and all its players
 */
struct client *delete_game(struct client*top, Game *game){
    struct client *head = top;
    // Create a msg struct
    Msg *msg;
    if((msg = malloc(sizeof(Msg))) == NULL) {
        perror("malloc");
        exit(1);
    }
    msg->msg_type = ENDING_GAME;

    // Create a payload
    EndPayload *payload;
    if ((payload = malloc(sizeof(EndPayload))) == NULL) {
	free(msg);
        perror("malloc");
        exit(1);
    }

    payload->winner = 0;
    parseEndPayload(payload, msg->payload, 0);
    char buf[BUFSIZE];
    encode(msg, buf, BUFSIZE);

    // deletes each player, skipping if the socket is -1 (player already disconnected and removed)
    for (int i = 0; i<game->num_players; i++){
	int soc = game->players[i].socket;
	if (soc==-1){
            continue;
	}
	if(write(game->players[i].socket, buf, E_MSG_SIZE) != E_MSG_SIZE){
	    perror("write");
	    game->players[i].socket = -1;
	}
	head = removeclient(top, soc);
	game->players[i].game = NULL;
	game->players[i].socket = -1;
    }
    free(msg);
    free(payload);
    free(game);
    return head;
}

/*
 * Return true if a player has won their game given the tiles in their hand
 * A players wins if they have at least 3 sets of three of a kind tiles, and at least 14 tiles in their hand and revealed list total
 */
bool win(Player *player) {
    int three_of_a_kind = 0;
    Tile seen_tiles[MAX_HAND_SIZE] = {'\0'};
    int num_seen = 0;
    int num_tiles = 0;
    bool seen;
    int num_same;
    
    // brute force check
    // first add all of their tiles to an array
    Tile player_tiles[MAX_HAND_SIZE] = {'\0'};
    for (int i = 0; i<MAX_HAND_SIZE && player->hand[i].category!='\0' && player->hand[i].value!='\0'; i++){
	player_tiles[num_tiles] = player->hand[i];
	num_tiles++;
    }
    for (int i = 0; i<MAX_HAND_SIZE && player->revealed[i].category!='\0' && player->revealed[i].value!='\0'; i++){
        player_tiles[num_tiles] = player->revealed[i];
        num_tiles++;
    }
    if(num_tiles<14){
	return false;
    }
	
    for (int i = 0; i<num_tiles; i++){

	// check if tile has been seen and checked already
	seen = false;
        for(int k=0; k<num_seen && !seen; k++){
            if(seen_tiles[k].category==player_tiles[i].category&&seen_tiles[k].value==player_tiles[i].value){
                seen = true;
	    }
        }

	// if not seen yet, add to seen and check for three of a kind
	if(!seen){
	    seen_tiles[num_seen]=player_tiles[i];
	    num_seen++;
	    // check if three of a kind
	    num_same = 0;
	    for(int j=0; j<num_tiles; j++){
	        if(player_tiles[i].category == player_tiles[j].category&&player_tiles[i].value == player_tiles[j].value){
		    num_same++;
		}
	    }
	    if(num_same>=3){
                three_of_a_kind ++;
	    }
	}
    }
    return three_of_a_kind>=3;
}

/*
 * End a game, given the player number of the winner, or 0 if no winner
 */
int end_game(Game *game, int player_number){
    // Create a msg struct
    Msg *msg;
    if((msg = malloc(sizeof(Msg))) == NULL) {
        perror("malloc");
        exit(1);
    }
    msg->msg_type = ENDING_GAME;

    // Create a payload
    EndPayload *payload;
    if ((payload = malloc(sizeof(EndPayload))) == NULL) {
        perror("malloc");
        exit(1);
    } 

    payload->winner = player_number+1;
    parseEndPayload(payload, msg->payload, 0);
    char buf[BUFSIZE];
    encode(msg, buf, BUFSIZE);

    for (int i = 0; i<PLAYER_NUM; i++){
        // write msg
        size_t msg_len = E_MSG_SIZE;
        size_t total_written = 0;
        ssize_t nbytes=0;         
        const char *ptr =buf;
        while(total_written<msg_len){
            nbytes = write(game->players[i].socket, ptr+total_written, msg_len-total_written);
            if(nbytes<0){
                perror("write");
                printf("Connection to client lost\n");
                free(msg);
                free(payload);
                return -1;
            }
            total_written = total_written+nbytes;
        }
    }

    free(msg);
    free(payload);
    return 0;
}

/*
 * Make an announcement with the given parameters to players, based on the action performed
 */
int make_announcement(Game *game, char action, int player_number, char *details){
    // Create a msg struct
    Msg *msg;
    if((msg = malloc(sizeof(Msg))) == NULL) {
        perror("malloc");
        exit(1);
    }
    msg->msg_type = ANNOUNCEMENT;

    // Create a payload
    AnnouncementPayload *payload;
    if ((payload = malloc(sizeof(AnnouncementPayload))) == NULL) {
        perror("malloc");
        exit(1);
    }

    payload->player_number = player_number;
    payload->action = action;
    payload->details_len = strlen(details);
    strncpy(payload->details, details, sizeof(payload->details));
    payload->details[sizeof(payload->details)-1] = '\0';
    parseAnnouncementPayload(payload, msg->payload, 0);
    char buf[BUFSIZE];
    encode(msg, buf, BUFSIZE);

    if(action==DREW_TILE){ // for draw tile announcement, tell only the player who drew the tile what the tile was, and tell other players that a tile was drawn
	
	// write msg
        size_t msg_len = payload->details_len + 7;
        size_t total_written = 0;
        ssize_t nbytes=0;         
        const char *ptr =buf;
        while(total_written<msg_len){
            nbytes = write(game->players[player_number-1].socket, ptr+total_written, msg_len-total_written);
            if(nbytes<0){
                perror("write");
                printf("Connection to client lost\n");
                free(msg);
                free(payload);
                return -1;
            }
            total_written = total_written+nbytes;
        }

	payload->details_len = 0;
        strncpy(payload->details, "", sizeof(payload->details));
	payload->details[sizeof(payload->details)-1] = '\0';
	parseAnnouncementPayload(payload, msg->payload, 0);
        encode(msg, buf, BUFSIZE);
	for (int i = 0; i<PLAYER_NUM; i++){
            if(i!=player_number-1){
                // write msg
                size_t msg_len = payload->details_len + 7;
                size_t total_written = 0;
		ssize_t nbytes=0;    
                const char *ptr =buf;
                while(total_written<msg_len){
                    nbytes = write(game->players[i].socket, ptr+total_written, msg_len-total_written);
                    if(nbytes<0){
                        perror("write");
                        printf("Connection to client lost\n");
                        free(msg);
                        free(payload);
                        return -1;
                    }
                    total_written = total_written+nbytes;
                }   
            }
        }
    }else if(action==ERROR){ // if error, only inform the player who performed the action that an error occurred
	// write msg
        size_t msg_len = payload->details_len + 7;
        size_t total_written = 0;
        ssize_t nbytes=0;    
        const char *ptr =buf;
        while(total_written<msg_len){
            nbytes = write(game->players[player_number-1].socket, ptr+total_written, msg_len-total_written);
            if(nbytes<0){
                perror("write");
                printf("Connection to client lost\n");
                free(msg);
                free(payload);
                return -1;
            }
            total_written = total_written+nbytes;
        }   
    } else{ // otherwise, send same announcement to all players
        for (int i = 0; i<PLAYER_NUM; i++){
            // write msg
            size_t msg_len = payload->details_len + 7;
            size_t total_written = 0;
            ssize_t nbytes=0;    
            const char *ptr =buf;
            while(total_written<msg_len){
                nbytes = write(game->players[i].socket, ptr+total_written, msg_len-total_written);
                if(nbytes<0){
                    perror("write");
                    printf("Connection to client lost\n");
                    free(msg);
                    free(payload);
                    return -1;
                }
                total_written = total_written+nbytes;
            }    
        }
    }

    free(msg);
    free(payload);
    return 0;
}

/*
 * Handle the situation of stealing a tile, given the index of the player performing the action and the game its in.
 * Return -1 if unable to perform the action
 */
int handle_steal_tile(Game *game, int player_index){
    Tile to_steal = game->discard[game->discard_count-1];
    Player *player = &(game->players[player_index]);
    Tile tile1 = game->tile_responses1[player_index];
    Tile tile2 = game->tile_responses2[player_index];
    
    // Find the indices of the tiles, if the tiles are the same, index_tile1 is the index of the first occurence and index_tile2 is that of the second
    int index_tile1 = -1;
    int index_tile2 = -1;
    for (int i = 0; i < MAX_HAND_SIZE; i++) {
        if (index_tile1 == -1 && player->hand[i].category == tile1.category && player->hand[i].value == tile1.value) {
            index_tile1 = i;
        } else if (index_tile2==-1 && player->hand[i].category == tile2.category && player->hand[i].value == tile2.value) {
            index_tile2 = i;
        }
    }

    // if a tile is not in the player's hand, return unsuccessful
    if(index_tile1 == -1 || index_tile2==-1){
        return -1; // at least one tile not found in player's hand
    }

    // else
    // find the first free spot in the player's revealed list.
    int empty_index = 0;
    while (empty_index<MAX_HAND_SIZE-2 
		    && player->revealed[empty_index].category != '\0' && player->revealed[empty_index].value != '\0'){
        empty_index++;
    }
    if(empty_index == MAX_HAND_SIZE-2){
        return -1;
    }
    //add the tiles from the player's hand and the stolen tile to the player's revealed list
    player->revealed[empty_index] = tile1;
    player->revealed[empty_index+1] = tile2;
    player->revealed[empty_index+2] = to_steal;

    // remove the tiles from the player's hand
    int higher_index, lower_index;
    if(index_tile1>index_tile2){
        higher_index = index_tile1;
	lower_index = index_tile2;
    } else{
        higher_index = index_tile2;
        lower_index = index_tile1;
    }
    for (int i = higher_index; i<MAX_HAND_SIZE-1; i++){
        player->hand[i] = player->hand[i+1];
    }
    player->hand[MAX_HAND_SIZE-1].category = '\0';
    player->hand[MAX_HAND_SIZE-1].value = '\0';
    for (int i = lower_index; i<MAX_HAND_SIZE-1; i++){
        player->hand[i] = player->hand[i+1];
    }
    player->hand[MAX_HAND_SIZE-1].category = '\0';
    player->hand[MAX_HAND_SIZE-1].value = '\0';

    // remove the stolen tile from the discard pile
    game->discard[game->discard_count-1].category = '\0';
    game->discard[game->discard_count-1].value = '\0';
    game->discard_count = game->discard_count-1;

    // clear the responses list to prepare for the next round
    for(int i = 0; i<PLAYER_NUM; i++){
        game->responses[i] = '\0';
    }
    return 0;
}






