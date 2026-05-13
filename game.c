#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"

/*
 * Helper function that populates a tiles array
 */
void _initialize_tiles(Tile *tiles) {
    int tile_index = 0;
    char suits[3] = {'D', 'B', 'C'}; // suits are D-Dots, B-Bamboo, C-Characters. Each suit has possible values from 1-9
    char suit_values[9] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    char winds[4] = {'n', 's', 'e', 'w'}; // W-Winds have the following possible values: n-north, s-south, e-east, w-west
    char dragons[3] = {'r', 'g', 'b'}; // G-draGons have the following possible values: r-red, g-green, b-blue
    
    // populate suit tiles
    for (int i = 0; i<3; i++) { // three suits
        for (int j = 0; j<9; j++) { // nine values
            for (int k = 0; k<4; k++) { // generate 4 tiles of each kind
                tiles[tile_index].category = suits[i];
                tiles[tile_index].value = suit_values[j];
                tile_index = tile_index+1;
            }
        }
    }
    
    // populate wind tiles
    for (int i = 0; i<4; i++) { // four values
        for (int k = 0; k<4; k++) { // generate 4 tiles of each kind
            tiles[tile_index].category = 'W';
            tiles[tile_index].value = winds[i];
            tile_index = tile_index+1;
        }
    }
    
    // populate dragon tiles
    for (int i = 0; i<3; i++) { // three values
        for (int k = 0; k<4; k++) { // generate 4 tiles of each kind
            tiles[tile_index].category = 'G';
            tiles[tile_index].value = dragons[i];
            tile_index = tile_index+1;
        }
    }
}

/*
 * Helper function that shuffles a tiles array
 */
void _shuffle_tiles(Tile *tiles) {
    for (int i = TILE_NUM-1; i>0; i--) {
        int new_index = rand() % (i+1);
	if (new_index != i){
	    char temp_cat = tiles[new_index].category;
	    char temp_val = tiles[new_index].value;
	    tiles[new_index].category = tiles[i].category;
	    tiles[new_index].value = tiles[i].value;
	    tiles[i].category = temp_cat;
	    tiles[i].value = temp_val;
	}
    }
}

/*
 * Helper function that initializes player numbers
 */
void _initialize_players(Game *game, Player *players) {
    for (int i = 0; i<4; i++) {
        players[i].player_number = i+1;
	players[i].game = game;
    }
}

/*
 * Helper function that deals tiles to each player
 */
void _deal_tiles(Tile *tiles, Player *players) {
    int tile_index = 0;
    for (int i = 0; i<4; i++) {
        for (int tile_num = 0; tile_num<STANDARD_HAND_SIZE; tile_num++) {
            players[i].hand[tile_num].category = tiles[tile_index].category;
	    players[i].hand[tile_num].value = tiles[tile_index].value;
	    players[i].revealed[tile_num].category = '\0';
            players[i].revealed[tile_num].value = '\0';
	    tile_index++;
	}
	players[i].hand[MAX_HAND_SIZE-1].category = '\0';
	players[i].hand[MAX_HAND_SIZE-1].value = '\0';
	players[i].revealed[MAX_HAND_SIZE-1].category = '\0';
        players[i].revealed[MAX_HAND_SIZE-1].value = '\0';

    }
}

/*
 * Initializes a Mahjong game given a pointer to a Game struct, and an array of 4 player sockets. Generates and shuffles tiles, and deals STANDARD_HAND_SIZE tiles to each player.
 */
int init_game(Game *game) {
    game->curr_player = rand() % (4);
    game->num_players = 0;
    game->game_status = WAITING;
    game->discard_count = 0;

    for(int i = 0; i<PLAYER_NUM; i++){
        game->responses[i] = '\0';
    }

    _initialize_tiles(game->tiles);
    _shuffle_tiles(game->tiles);
    _initialize_players(game, game->players);
    _deal_tiles(game->tiles, game->players);
    
    game->curr_tile = 52;

    return 0; 
}

/*
 * Creates and adds a player to a game, given the player's socket number. Returns a pointer
 * to the player struct. Returns NULL if the player list is full.
 */
Player *add_player(Game *game, int soc) {
    if (game->num_players == 4) {
        return NULL;
    } else {
        game->players[game->num_players].socket = soc;
	game->num_players = game->num_players + 1;
	if (game->num_players == 4) {
	    game->game_status = READY_TO_START;
	}
       return &game->players[game->num_players-1];	
    }
}

/*
 * initialize a game on the client side
 */
int init_client_game(ClientGame *game) {
    game->curr_player = 0;
    game->num_players = 0;
    game->game_status = WAITING;
    game->discard_count = 0;
    game->players_index = -1;
    for(int i=0; i<PLAYER_NUM; i++){
	memset(game->players[i].hand, 0, sizeof(game->players[i].hand));
        memset(game->players[i].revealed, 0, sizeof(game->players[i].revealed));
    }
    return 0;
}

