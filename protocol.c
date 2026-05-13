#include <string.h>
#include <stdio.h>

#include "protocol.h"

/*
 * Takes a Msg struct and a pointer to a char buffer with a given size, and encodes the message to the buffer. Return -1 if there is not enough room.
 */
int encode(Msg *msg, char *buf, int buffer_size) {
    int msg_size = 1 + strlen(msg->payload) + 2; // need to format as [msg_type][payload][\r\n]
    if (msg_size + 1 > buffer_size) { // leaving room for \0 at the end, if the encoding is too big for the buffer, return -1 as an error
        return -1;
    }
    else{ // otherwise, copy the message info into the buffer
        buf[0] = msg->msg_type; // char representing the msg type
	strncpy(buf+1, msg->payload, buffer_size-1);
	buf[msg_size - 2] = '\r';
	buf[msg_size - 1] = '\n';
	buf[msg_size] = '\0'; // add '\0' just to be safe

	return 0;
    }
}

/*
 * Takes a buffer and a pointer to a Msg and decodes the data in the buffer into a message struct. 
 */
int decode(Msg *msg, char *buf) {
    if (strlen(buf) == 0) {
        return -1;
    } 
    else{
	msg->msg_type = buf[0];
	strncpy(msg->payload, buf+1, MAX_PAYLOAD);
	msg->payload[MAX_PAYLOAD-1] = '\0';
	return 0;
    }
}

/*
 * If direction flag is 0, encode from a JoinPayload struct to a string encoding.
 * If direction flag is 1, decode from string to JoinPayload struct.
 */
int parseJoinPayload(JoinPayload *payload_struct, char *payload, int direction) {
    if (direction == 0) {
        payload[0] = payload_struct->player_number + '0';
	payload[1] = payload_struct->players_found + '0';
	payload[2] = '\0';
	return 0;
    }
    else if (direction ==1) {
        payload_struct->player_number = payload[0] - '0';
	payload_struct->players_found = payload[1] - '0';
	return 0;
    }else {
        return -1;
    }
}

/*
 * If direction flag is 0, encode from a StartPayload struct to a string encoding.
 * If direction flag is 1, decode from string to StartPayload struct.
 */
int parseStartPayload(StartPayload *payload_struct, char *payload, int direction) {
    if (direction == 0) {
        payload[0] = payload_struct->player_turn + '0';
	for (int i = 0; i<STANDARD_HAND_SIZE; i++) {
	    payload[1+2*i] = payload_struct->hand[i].category;
	    payload[1+2*i+1] = payload_struct->hand[i].value;
	}
        payload[27] = '\0';
        return 0;
    }
    else if (direction ==1) {
        payload_struct->player_turn = payload[0] - '0';
        for (int i = 0; i<STANDARD_HAND_SIZE; i++) {
            payload_struct->hand[i].category = payload[1+2*i];
	    payload_struct->hand[i].value = payload[1+2*i+1];
        }
        return 0;
    }else {
        return -1;
    }
}

/*
 * If direction flag is 0, encode from a DrawPlayPayload struct to a string encoding.
 * If direction flag is 1, decode from string to DrawPlayPayload struct.
 */
int parseDrawPlayPayload(DrawPlayPayload *payload_struct, char *payload, int direction) {
    if (direction == 0) {
        payload[0] = payload_struct->player_turn + '0';
        payload[1] = '\0';
        return 0;
    }
    else if (direction ==1) {
        payload_struct->player_turn = payload[0] - '0';
        return 0;
    }else {
        return -1;
    }
}

/*
 * If direction flag is 0, encode from a RespondPayload struct to a string encoding.
 * If direction flag is 1, decode from string to RespondPayload struct.
 */
int parseRespondPayload(RespondPayload *payload_struct, char *payload, int direction) {
    if (direction == 0) {
        payload[0] = payload_struct->last_player_turn + '0';
	payload[1] = payload_struct->tile.category;
	payload[2] =  payload_struct->tile.value;
        payload[3] = '\0';
        return 0;
    }
    else if (direction ==1) {
        payload_struct->last_player_turn = payload[0] - '0';
	payload_struct->tile.category = payload[1];
	payload_struct->tile.value = payload[2];
        return 0;
    }else {
        return -1;
    }
}

/*
 * If direction flag is 0, encode from a EndPayload struct to a string encoding.
 * If direction flag is 1, decode from string to EndPayload struct.
 */
int parseEndPayload(EndPayload *payload_struct, char *payload, int direction) {
    if (direction == 0) {
        payload[0] = payload_struct->winner + '0';
        payload[1] = '\0';
        return 0;
    }
    else if (direction ==1) {
        payload_struct->winner = payload[0] - '0';
        return 0;
    }else {
        return -1;
    }
}

/*
 * If direction flag is 0, encode from an AnnouncementPayload struct to a string encoding.
 * If direction flag is 1, decode from string to AnnouncementPayload struct.
 */
int parseAnnouncementPayload(AnnouncementPayload *payload_struct, char *payload, int direction) {
    if (direction == 0) {
        payload[0] = payload_struct->player_number + '0';
        payload[1] = payload_struct->action;
	payload[2] = (payload_struct->details_len / 10) + '0';
        payload[3] = (payload_struct->details_len % 10) + '0';
	strncpy(&payload[4], payload_struct->details, payload_struct->details_len);
	payload[4+payload_struct->details_len] = '\0';
        return 0;
    }
    else if (direction ==1) {
        payload_struct->player_number = payload[0] - '0';
	payload_struct->action = payload[1];
	payload_struct->details_len = (payload[2]-'0')*10 + (payload[3]-'0');
	strncpy(payload_struct->details, &payload[4], payload_struct->details_len);
        payload_struct->details[payload_struct->details_len] = '\0';

        return 0;
    }else {
        return -1;
    }
}

/*
 * If direction flag is 0, encode from a ClientPlayPayload struct to a string encoding.
 * If direction flag is 1, decode from string to ClientPlayPayload struct.
 */
int parseClientPlayPayload(ClientPlayPayload *payload_struct, char *payload, int direction) {
    if (direction == 0) {
        payload[0] = payload_struct->tile.category;
	payload[1] = payload_struct->tile.value;
        payload[2] = '\0';
        return 0;
    }
    else if (direction ==1) {
        payload_struct->tile.category = payload[0];
	payload_struct->tile.value = payload[1];
        return 0;
    }else {
        return -1;
    }
}

/*
 * If direction flag is 0, encode from a ClientRespondPayload struct to a string encoding.
 * If direction flag is 1, decode from string to ClientRespondPayload struct.
 */
int parseClientRespondPayload(ClientRespondPayload *payload_struct, char *payload, int direction) {
    if (direction == 0) {
        payload[0] = payload_struct->response;
	if(payload[0] == 'p'){
            payload[1] = '\0';
	} else{
            payload[1] = payload_struct->tiles[0].category;
	    payload[2] = payload_struct->tiles[0].value;
	    payload[3] = payload_struct->tiles[1].category;
	    payload[4] = payload_struct->tiles[1].value;
	    payload[5] = '\0';
	}
        return 0;
    }
    else if (direction ==1) {
        payload_struct->response = payload[0];
	if(payload[0] == 's'){
            payload_struct->tiles[0].category = payload[1];
	    payload_struct->tiles[0].value = payload[2];
	    payload_struct->tiles[1].category = payload[3];
	    payload_struct->tiles[1].value = payload[4];
	}
        return 0;
    }else {
        return -1;
    }
}


