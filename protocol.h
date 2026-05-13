#ifndef _PROTOCOL_H_
#define _PROTOCOL_H_

#include <stdint.h>
#include "game.h"

#define MAX_PAYLOAD 256

// server to client message constants
#define JOINING_GAME 'J'
#define STARTING_GAME 'S'
#define ENDING_GAME 'E'
#define DRAW_PHASE 'D'
#define PLAY_PHASE 'P'
#define RESPOND_PHASE 'R'
#define ANNOUNCEMENT 'A'
#define J_MSG_SIZE 5 
#define S_MSG_SIZE 30
#define E_MSG_SIZE 4
#define D_MSG_SIZE 4
#define P_MSG_SIZE 4
#define R_MSG_SIZE 6

// announcement actions
#define DREW_TILE '0'
#define PLAYED_TILE '1'
#define STOLE_TILE '2'
#define PASSED '3'
#define ERROR '4'


// client to server message constants
#define DRAW_PHASE_ACTION 'd'
#define PLAY_PHASE_ACTION 'p'
#define RESPOND_PHASE_ACTION 'r'
#define LEAVE 'l'
#define PLAY_MSG_SIZE 5
#define RESPOND_MSG_SIZE_PASS 4
#define RESPOND_MSG_SIZE_STEAL 8

typedef struct {
    char msg_type;// a character code representing the type of message being send
    char payload[MAX_PAYLOAD]; // null-terminated string representing the data being sent
} Msg;

typedef struct {
    int player_number; // player number from 1-4
    int players_found; // number of players in the game so far (from 1-4)
} JoinPayload;

typedef struct {
    int player_turn; // player number whose turn it is (from 1-4), should be 1
    Tile hand[STANDARD_HAND_SIZE]; // tiles in the player's hand
} StartPayload;

typedef struct {
    int player_turn; // player number whose turn it is to draw or play a card (from 1-4)
} DrawPlayPayload;

typedef struct {
    int last_player_turn; // player number who just played a card (from 1-4)
    Tile tile; // tiles that was played
} RespondPayload;

typedef struct {
    int winner; // 0 if no winner, otherwise the player number of the winner
} EndPayload;

typedef struct {
    int player_number; // player number of the player who just performed an action
    char action; // letter representing what action was performed
    int details_len; // number of characters passed in details, must be less than MAX_PAYLOAD-4
    char details[MAX_PAYLOAD-4]; // null-terminated, further details about the action, structured differently based on what the action was 
} AnnouncementPayload;

typedef struct {
    Tile tile; // the tile being played
} ClientPlayPayload;

typedef struct {
    char response; // 'p' to pass, 's' to steal
    Tile tiles[2]; // if steal, the two tiles to combine with the stolen tile
} ClientRespondPayload;

int encode(Msg *msg, char *buf, int buffer_size); // encode msg to a string to be sent, ensure the length of the message fits in the buffer
int decode(Msg *msg, char *buf); // decode data from buf to msg

// parse between a JoinPayload struct and its string encoding, based on direction flag. If direction flag is 0, encode from a JoinPayload struct to a string encoding. If direction flag is 1, decode from string to JoinPayload struct.
int parseJoinPayload(JoinPayload *payload_struct, char *payload, int direction);

// parse between a StartPayload struct and its string encoding, based on direction flag. 
int parseStartPayload(StartPayload *payload_struct, char *payload, int direction);

// parse between a DrawPlayPayload struct and its string encoding, based on direction flag. 
int parseDrawPlayPayload(DrawPlayPayload *payload_struct, char *payload, int direction);

// parse between a RespondPayload struct and its string encoding, based on direction flag. 
int parseRespondPayload(RespondPayload *payload_struct, char *payload, int direction);

// parse between an EndPayload struct and its string encoding, based on direction flag. 
int parseEndPayload(EndPayload *payload_struct, char *payload, int direction);

// parse between an AnnouncementPayload struct and its string encoding, based on direction flag. 
int parseAnnouncementPayload(AnnouncementPayload *payload_struct, char *payload, int direction);

// parse between a ClientPlayPayload struct and its string encoding, based on direction flag. 
int parseClientPlayPayload(ClientPlayPayload *payload_struct, char *payload, int direction);

// parse between a ClientRespondPayload struct and its string encoding, based on direction flag. 
int parseClientRespondPayload(ClientRespondPayload *payload_struct, char *payload, int direction);


#endif
