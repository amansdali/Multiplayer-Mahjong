#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>

#include "socket.h"
#include "protocol.h"
#include "game.h"

#ifndef PORT
  #define PORT 38558
#endif

#define BUFSIZE 512
#define PENDING_MOVE_DRAW 0
#define PENDING_MOVE_PLAY 1
#define PENDING_MOVE_RESPOND 2
#define PENDING_MOVE_NONE 3

int handle_message(char *buf, ClientGame *game, int prev_pending_move);
void handle_user_input(char *buf, ClientGame *game, int soc, int pending_move);
void print_tiles(Tile *tiles, int max_tiles);

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

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: client hostname\n");
        exit(1);
    }
    int soc = connect_to_server(PORT, argv[1]);

    // game information
    ClientGame *game = malloc(sizeof(ClientGame));
    if (game == NULL) {
        perror("malloc");
	exit(1);
    }
    if (init_client_game(game) < 0) {
        fprintf(stderr, "Error initializing game\n");
	exit(1);
    }
    int pending_move = PENDING_MOVE_NONE;

    // select variables
    int max_fd;
    fd_set allset;
    fd_set rset;
    struct timeval tv;
    FD_ZERO(&allset);
    FD_SET(STDIN_FILENO, &allset);
    FD_SET(soc, &allset);
    if(soc>STDIN_FILENO){
        max_fd = soc;
    } else{
	max_fd = STDIN_FILENO;
    }

    // Receive messages
    char buf[BUFSIZE] = {'\0'};
    int inbuf = 0;           // How many bytes currently in buffer?
    int room = sizeof(buf);  // How many bytes remaining in buffer?
    char *after = buf;       // Pointer to position after the data in buf

    int nbytes;
    bool running = true;
    while(running){
        // select
	rset = allset;
	tv.tv_sec = 10;
        tv.tv_usec = 0;
	select(max_fd + 1, &rset, NULL, NULL, &tv);
    
	if (FD_ISSET(soc, &rset)){

	    nbytes = read(soc, after, room);
	    if(nbytes<0) {
		perror("read");
		running = false;
                continue;
	    }else if(nbytes == 0){
                printf("connection closed by server, closing program");
		running = false;
		continue;
	    }
            // update inbuf
            inbuf = inbuf + nbytes;

            int where;

            // The loop condition below calls find_network_newline
            // to determine if a full line has been read from the client.
            // Note: we use a loop here because a single read might result in
            // more than one full line.
            while ((where = find_network_newline(buf, inbuf)) > 0) {
                // where is now the index into buf immediately after
                // the first network newline
                buf[where-2]='\0';

	        if ((pending_move = handle_message(buf, game, pending_move)) < 0) {
                    running = false;
	            break;
	        }
                // Update inbuf and remove the full line from the buffer
                inbuf = inbuf - where;
                memmove(buf, buf+where, inbuf);
            }
            // update after and room, in preparation for the next read.
	    after = buf+inbuf;
	    room = sizeof(buf) - inbuf;
	    if (room == 0) {
                fprintf(stderr, "Server sent malformed message\n");
                running = false;
		break;
            }
	}

	if (FD_ISSET(STDIN_FILENO, &rset)) {
            char user_buf[BUFSIZE];

	    if(fgets(user_buf, sizeof(user_buf), stdin) != NULL){
                handle_user_input(user_buf, game, soc, pending_move);
	    } else{
	         if (feof(stdin)) {
	             printf("EOF of stdin reached, closing program\n");
		     running = false;
		     continue;
		 }else if (ferror(stdin)){
                     perror("stdin");
		     running = false;
		     continue;
		 }
	    }
	} 

    }

    free(game);
    // close the connection and return
    close(soc);
    return 0;
}

int handle_message(char *buf, ClientGame *game, int prev_pending_move) {

    Msg *msg = malloc(sizeof(Msg));
    if(msg == NULL) {
        perror("malloc");
	free(game);
	exit(1);
    }
    decode(msg, buf);
    int pending_move = 0;
    switch(msg->msg_type) {
        case JOINING_GAME:
	    JoinPayload *jpayload = malloc(sizeof(JoinPayload));
	    if (jpayload == NULL) {
                perror("malloc");
		free(msg);
		free(game);
		exit(1);
	    }
	    parseJoinPayload(jpayload, msg->payload, 1);

	    printf("-----------------------------------------------------\n");

	    if (game->players_index != jpayload->player_number -1){
                game->players_index =jpayload->player_number-1;
	        printf("Welcome, player %d.\n", jpayload->player_number);
	    }
	    game->num_players = jpayload->players_found;

	    printf("Joining game, %d/4\n", jpayload->players_found);
	    
	    free(jpayload);

	    pending_move = PENDING_MOVE_NONE;
	    break;
	case STARTING_GAME:
	    StartPayload *spayload = malloc(sizeof(StartPayload));
            if (spayload == NULL) {
                perror("malloc");
		free(msg);
                free(game);
                exit(1);
            }
            parseStartPayload(spayload, msg->payload, 1);
            game->curr_player = spayload->player_turn-1;
            for(int i = 0; i<STANDARD_HAND_SIZE; i++){
                game->players[game->players_index].hand[i] = spayload->hand[i];
	    }
	    game->players[game->players_index].hand[STANDARD_HAND_SIZE].category = '\0';
	    game->players[game->players_index].hand[STANDARD_HAND_SIZE].value = '\0';

	    printf("-----------------------------------------------------\n");
            printf("The game has started. Player %d starts. Enter Q to quit game, enter V to view current game information, enter H to view hand.\n", spayload->player_turn);
	    printf("Your hand is:\n");
	    print_tiles(game->players[game->players_index].hand, MAX_HAND_SIZE);
	    printf("Your revealed tiles are:\n");
	    print_tiles(game->players[game->players_index].revealed, MAX_HAND_SIZE);
	    for(int i = 0; i<4; i++){
	        if (i != game->players_index){
	            printf("Player %d's revealed tiles are:\n", i+1);
		    print_tiles(game->players[i].revealed, MAX_HAND_SIZE);
		}
	    }
	    printf("The discarded tiles are:\n");
	    game->discard[game->discard_count].category = '\0';
	    game->discard[game->discard_count].value = '\0';
	    print_tiles(game->discard, TILE_NUM);

	    free(spayload);

	    pending_move = PENDING_MOVE_NONE;
	    break;
	case ENDING_GAME:
	    EndPayload *epayload = malloc(sizeof(EndPayload));
            if (epayload == NULL) {
                perror("malloc");
		free(msg);
                free(game);
                exit(1);
            }
            parseEndPayload(epayload, msg->payload, 1);
            game->game_status = GAME_OVER;
            printf("-----------------------------------------------------\n");
            if(epayload->winner == 0){
                printf("Game has closed. Thanks for playing!\n");
		free(epayload);
		free(msg);
		return -1;
            } else{
                printf("Game is over, player %d won! Enter 'Q' to leave.\n", epayload->winner);
		free(epayload);
		pending_move = PENDING_MOVE_NONE;
                break;
            }
	case DRAW_PHASE:
	    DrawPlayPayload *dpayload = malloc(sizeof(DrawPlayPayload));
            if (dpayload == NULL) {
                perror("malloc");
		free(msg);
                free(game);
                exit(1);
            }
            parseDrawPlayPayload(dpayload, msg->payload, 1);
	    game->curr_player = dpayload->player_turn-1;
	    printf("-----------------------------------------------------\n");
	    if(game->curr_player == game->players_index){
		printf("Your turn to draw a tile. Type 'd' to draw a tile.\n");
		pending_move = PENDING_MOVE_DRAW;
	    } else{
                printf("Player %d's turn to draw a tile.\n", dpayload->player_turn);
		pending_move = PENDING_MOVE_NONE;
	    }

	    free(dpayload);
	    break;
	case PLAY_PHASE:
	    DrawPlayPayload *ppayload = malloc(sizeof(DrawPlayPayload));
            if (ppayload == NULL) {
                perror("malloc");
		free(msg);
                free(game);
                exit(1);
            }
            parseDrawPlayPayload(ppayload, msg->payload, 1);
            game->curr_player = ppayload->player_turn-1;
            printf("-----------------------------------------------------\n");
            if(game->curr_player == game->players_index){
                printf("Your turn to play a tile. Enter the code of the tile you want to play.\n");
		pending_move = PENDING_MOVE_PLAY;
            } else{
                printf("Player %d's turn to play a tile.\n", ppayload->player_turn);
                pending_move = PENDING_MOVE_NONE;
            }
            
	    free(ppayload);
            break;
	case RESPOND_PHASE:
	    RespondPayload *rpayload = malloc(sizeof(RespondPayload));
            if (rpayload == NULL) {
                perror("malloc");
		free(msg);
                free(game);
                exit(1);
            }
            parseRespondPayload(rpayload, msg->payload, 1);
            printf("-----------------------------------------------------\n");
            if(rpayload->last_player_turn == game->players_index){
                printf("You have played tile %c%c. Wait for other players to respond to your move.\n", rpayload->tile.category, rpayload->tile.value);
                pending_move = PENDING_MOVE_NONE;
	    } else{
                printf("Player %d played %c%c. Enter s to try to steal and p to pass. If you steal, also enter the two tiles to reveal with the stolen tile.\n", rpayload->last_player_turn+1, rpayload->tile.category, rpayload->tile.value);
                pending_move = PENDING_MOVE_RESPOND;
	    }

            free(rpayload);
            break;
	case ANNOUNCEMENT:
	    AnnouncementPayload *apayload = malloc(sizeof(AnnouncementPayload));
            if (apayload == NULL) {
                perror("malloc");
		free(msg);
                free(game);
                exit(1);
            }
            parseAnnouncementPayload(apayload, msg->payload, 1);
	    pending_move = PENDING_MOVE_NONE;
	    printf("-----------------------------------------------------\n");
	    switch(apayload->action){
                case DREW_TILE:
		    if(apayload->player_number == game->players_index+1){
                        printf("You drew %c%c.\n", apayload->details[0], apayload->details[1]);
			for (int i = 0; i<MAX_HAND_SIZE; i++){
                            if (game->players[game->players_index].hand[i].category == '\0' 
					    && game->players[game->players_index].hand[i].value == '\0'){
                                game->players[game->players_index].hand[i].category = apayload->details[0];
                                game->players[game->players_index].hand[i].value = apayload->details[1];
                                if (i+1 < MAX_HAND_SIZE){
                                    game->players[game->players_index].hand[i+1].category = '\0';
                                    game->players[game->players_index].hand[i+1].value = '\0';
                                }
                                break;
                            }
                        }
		    } else{
			printf("Player %d drew a tile.\n", apayload->player_number);
		    }
		    break;
		case PLAYED_TILE:
		    if(apayload->player_number != game->players_index+1){
		        printf("Player %d played %c%c.\n", apayload->player_number, apayload->details[0], apayload->details[1]);
			game->discard[game->discard_count].category = apayload->details[0];
			game->discard[game->discard_count].value = apayload->details[1];
                        game->discard_count = game->discard_count +1;
		    } else{
			printf("You played %c%c.\n", apayload->details[0], apayload->details[1]);
		        for (int i = 0; i<MAX_HAND_SIZE && game->players[game->players_index].hand[i].category != '\0' &&  game->players[game->players_index].hand[i].value != '\0'; i++){
                            if ( game->players[game->players_index].hand[i].category == apayload->details[0] &&  game->players[game->players_index].hand[i].value == apayload->details[1]){
                                 game->discard[game->discard_count] = game->players[game->players_index].hand[i];
                                 game->discard_count = game->discard_count +1;

                                 for (int j = i; j<MAX_HAND_SIZE-1; j++){
                                      game->players[game->players_index].hand[j] =  game->players[game->players_index].hand[j+1];
                                 }
                              game->players[game->players_index].hand[MAX_HAND_SIZE-1].category = '\0';
                               game->players[game->players_index].hand[MAX_HAND_SIZE-1].value = '\0';
                            break;
                            }
                        }
		    }
                    break;
		case STOLE_TILE:
		    ClientPlayer *player = &(game->players[apayload->player_number-1]);
		    if(apayload->player_number != game->players_index+1){
                        printf("Player %d stole %c%c. Using %c%c and %c%c\n", apayload->player_number, apayload->details[4], apayload->details[5], apayload->details[0], apayload->details[1], apayload->details[2], apayload->details[3]);
                    }else{
			printf("You stole %c%c. Using %c%c and %c%c\n", apayload->details[4], apayload->details[5], apayload->details[0], apayload->details[1], apayload->details[2], apayload->details[3]);
			int index_tile1 = -1;
                        int index_tile2 = -1;
			for (int i = 0; i < MAX_HAND_SIZE; i++) {
                            if (index_tile1 == -1 && player->hand[i].category == apayload->details[0] && player->hand[i].value == apayload->details[1]) {
                                index_tile1 = i;
                            } else if (index_tile2==-1 && player->hand[i].category == apayload->details[2] && player->hand[i].value == apayload->details[3]) {
                                index_tile2 = i;
                            }
                        }
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
		    }

		    int empty_index = 0;
                    while (empty_index<MAX_HAND_SIZE-2
                         && player->revealed[empty_index].category != '\0' && player->revealed[empty_index].value != '\0'){
                         empty_index++;
                    }
                    player->revealed[empty_index].category = apayload->details[0];
                    player->revealed[empty_index+1].category = apayload->details[2];
                    player->revealed[empty_index+2].category = apayload->details[4];
		    player->revealed[empty_index].value = apayload->details[1];
                    player->revealed[empty_index+1].value = apayload->details[3];
                    player->revealed[empty_index+2].value = apayload->details[5];

		    game->discard[game->discard_count-1].category = '\0';
                    game->discard[game->discard_count-1].value = '\0';
                    game->discard_count = game->discard_count-1;
                    break;
		case PASSED:
		    printf("Every player passed.\n");
                    break;
		case ERROR:
		    printf("There was an error with your last move: %s\n", apayload->details);
		    pending_move = prev_pending_move; // allows user to make their previous move again
		    break;
		default:
		    printf("unknown announcement\n");
		    break;
	    }

            free(apayload);
            break;

	default:
	    printf("unknown message\n");
	    return -1;
    }

    free(msg);
    return pending_move;
}

void write_helper(int soc, const char *buf, size_t len, ClientGame *game) {
    size_t total = 0;
    ssize_t n;
    while (total < len) {
        n = write(soc, buf + total, len - total);
        if (n < 0) {
            perror("write");
            printf("Connection to server lost, closing program\n");
            free(game);
            exit(1);
        }
        total += n;
    }
}


void handle_user_input(char *buf, ClientGame *game, int soc, int pending_move){
    printf("you wrote: %s", buf);
    if(strcmp(buf, "Q\n")==0) {
        // quit game
	printf("-----------------------------------------------------\n");
        printf("You chose to quit game\n");

        char leave_msg[3] = {LEAVE, '\r', '\n'};

	// write msg
        write_helper(soc, leave_msg, 3, game);

    } else if (strcmp(buf, "V\n")==0){
	// view game details
	printf("-----------------------------------------------------\n");
        printf("Details of the current game: Player %d's turn. Enter Q to quit game, enter V to view current game information, enter H to view hand.\n", game->curr_player+1);
        printf("Your hand is:\n");
        print_tiles(game->players[game->players_index].hand, MAX_HAND_SIZE);
        printf("Your revealed tiles are:\n");
        print_tiles(game->players[game->players_index].revealed, MAX_HAND_SIZE);
        for(int i = 0; i<PLAYER_NUM; i++){
            if (i != game->players_index){
                printf("Player %d's revealed tiles are:\n", i+1);
                print_tiles(game->players[i].revealed, MAX_HAND_SIZE);
            }
        }
        printf("The discarded tiles are:\n");
        game->discard[game->discard_count].category = '\0';
        game->discard[game->discard_count].value = '\0';
        print_tiles(game->discard, TILE_NUM);

    }else if (strcmp(buf, "H\n")==0){
        // view game details
        printf("-----------------------------------------------------\n");
        printf("Your hand is:\n");
        print_tiles(game->players[game->players_index].hand, MAX_HAND_SIZE);
    }

    else if(pending_move == PENDING_MOVE_NONE){
	if(game->curr_player == game->players_index){
	    printf("No move to be made right now.\n");
	} else{
            printf("No move to be made right now. It is player %d's turn.\n", game->curr_player+1);
	}
    } else if (pending_move == PENDING_MOVE_DRAW){
	if(strcmp(buf, "d\n")==0){
	    // draw card
	    printf("-----------------------------------------------------\n");
	    printf("You go to draw a tile\n");

	    char draw_msg[3] = {DRAW_PHASE_ACTION, '\r', '\n'};
            write_helper(soc, draw_msg, 3, game);
	} else{
            printf("Invalid move, enter d to draw a tile or Q to quit.\n");
	}
    } else if (pending_move == PENDING_MOVE_PLAY){
	printf("-----------------------------------------------------\n");
	if(strlen(buf) == 3 && strchr("DBCWG", buf[0]) && strchr("123456789nsewrgb", buf[1])){
	    // play card
            printf("You go to play tile %c%c\n", buf[0], buf[1]);
	    // Create a msg struct
            Msg *msg;
            if((msg = malloc(sizeof(Msg))) == NULL) {
                perror("malloc");
                free(game);
                exit(1);
            }
            msg->msg_type = PLAY_PHASE_ACTION;

            // Create a payload
            ClientPlayPayload *payload;
            if ((payload = malloc(sizeof(ClientPlayPayload))) == NULL) {
                perror("malloc");
		free(msg);
                free(game);
                exit(1);
            }

            // modify the payload and encode/write the message to the client
            payload->tile.category = buf[0];
            payload->tile.value = buf[1];
            parseClientPlayPayload(payload, msg->payload, 0);
            char outbuf[BUFSIZE];
            encode(msg, outbuf, BUFSIZE);
            
	    // write msg
	    size_t msg_len = PLAY_MSG_SIZE;
	    size_t total_written = 0;
	    ssize_t nbytes=0;
	    const char *ptr = outbuf;
	    while(total_written<msg_len){
                nbytes = write(soc, ptr+total_written, msg_len-total_written);
	        if(nbytes<0){
		    perror("write");
	            printf("Connection to server lost, closing program\n");
	            free(msg);
		    free(payload);
		    free(game);
	            exit(1);	    
		}
		total_written = total_written+nbytes;
	    }	

	    free(msg);
            free(payload);

	} else{
	    printf("Invalid move, enter the category followed by the value of the tile you want to play.\n");
	}
    } else{ //respond
        printf("-----------------------------------------------------\n");
        if(strcmp(buf, "p\n")==0){
            printf("You choose to pass\n");
            // Create a msg struct
            Msg *msg;
            if((msg = malloc(sizeof(Msg))) == NULL) {
                perror("malloc");
                free(game);
                exit(1);
            }
            msg->msg_type = RESPOND_PHASE_ACTION;

            // Create a payload
            ClientRespondPayload *payload;
            if ((payload = malloc(sizeof(ClientRespondPayload))) == NULL) {
                perror("malloc");
		free(msg);
                free(game);
                exit(1);
            }

            // modify the payload and encode/write the message to the client
            payload->response = buf[0];
            parseClientRespondPayload(payload, msg->payload, 0);
            char outbuf[BUFSIZE];
            encode(msg, outbuf, BUFSIZE);

	    // write msg
            size_t msg_len = RESPOND_MSG_SIZE_PASS;
            size_t total_written = 0;
            ssize_t nbytes=0;
            const char *ptr = outbuf;
            while(total_written<msg_len){
                nbytes = write(soc, ptr+total_written, msg_len-total_written);
                if(nbytes<0){
                    perror("write");
                    printf("Connection to server lost, closing program\n");
                    free(msg);
                    free(payload);
                    free(game);
                    exit(1);
                }
                total_written = total_written+nbytes;
            }

            free(msg);
            free(payload);
	}else if(strlen(buf) == 6 && buf[0]=='s' && strchr("DBCWG", buf[1]) && strchr("123456789nsewrgb", buf[2])
		       	&&strchr("DBCWG", buf[3]) && strchr("123456789nsewrgb", buf[4])){
	    printf("You choose to steal, using tiles %c%c and %c%c\n", buf[1], buf[2], buf[3], buf[4]);

	    // check for if the steal is valid (it the two tiles are the same as the tile that was just played)
	    if(buf[1]==buf[3]&&buf[1]==game->discard[game->discard_count-1].category&&buf[2]==buf[4]&&buf[2]==game->discard[game->discard_count-1].value){
	        // Create a msg struct
                Msg *msg;
                if((msg = malloc(sizeof(Msg))) == NULL) {
                    perror("malloc");
                    free(game);
                    exit(1);
                }
                msg->msg_type = RESPOND_PHASE_ACTION;

                // Create a payload
                ClientRespondPayload *payload;
                if ((payload = malloc(sizeof(ClientRespondPayload))) == NULL) {
                    perror("malloc");
		    free(msg);
                    free(game);
                    exit(1);
                }

                // modify the payload and encode/write the message to the client
                payload->response = buf[0];
	        payload->tiles[0].category = buf[1];
	        payload->tiles[0].value = buf[2];
	        payload->tiles[1].category = buf[3];
                payload->tiles[1].value = buf[4];
                parseClientRespondPayload(payload, msg->payload, 0);
                char outbuf[BUFSIZE];
                encode(msg, outbuf, BUFSIZE);
                
		// write msg
                size_t msg_len = RESPOND_MSG_SIZE_STEAL;
                size_t total_written = 0;
                ssize_t nbytes=0;
                const char *ptr = outbuf;   
                while(total_written<msg_len){
                    nbytes = write(soc, ptr+total_written, msg_len-total_written);
                    if(nbytes<0){
                        perror("write");
                        printf("Connection to server lost, closing program\n");
                        free(msg);
                        free(payload);
                        free(game);
                        exit(1);        
                    }
                    total_written = total_written+nbytes;
                }   

		
		free(msg);
                free(payload);
	    }else{
		printf("You can not steal with those tiles.\n");
	    }
	}else{
            printf("Invalid move, enter p to pass or s followed by the 2 tiles you want to reveal.\n");
	}
    }
    return;
}

/*
 * Prints a list of tiles to the terminal. A tile with category 'C' and value '1' is printed as follows:
 *  _
 * |C|
 * |1|
 *  -  
 */
void print_tiles(Tile *tiles, int max_tiles) {
    int index = 0;
    char first_line[max_tiles*3+1];
    char second_line[max_tiles*3+1];
    char third_line[max_tiles*3+1];
    char fourth_line[max_tiles*3+1];
    while (index<max_tiles && tiles[index].category != '\0' && tiles[index].value != '\0') {
        first_line[index*3] = ' ';
	first_line[index*3+1] = '_';
	first_line[index*3+2] = ' ';
	fourth_line[index*3] = ' ';
	fourth_line[index*3+1] = '-';
	fourth_line[index*3+2] = ' ';
	second_line[index*3] = '|';
	second_line[index*3+1] = tiles[index].category;
	second_line[index*3+2] = '|';
	third_line[index*3] = '|';
	third_line[index*3+1] = tiles[index].value;
	third_line[index*3+2] = '|';
	index ++;
    }
    first_line[index*3] = '\0';
    second_line[index*3] = '\0';
    third_line[index*3] = '\0';
    fourth_line[index*3] = '\0';
    printf("%s\n", first_line);
    printf("%s\n", second_line);
    printf("%s\n", third_line);
    printf("%s\n", fourth_line);
    return;
}





