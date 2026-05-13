#ifndef _GAME_H_
#define _GAME_H_

// constants
#define TILE_NUM 136
#define PLAYER_NUM 4
#define MAX_HAND_SIZE 14
#define STANDARD_HAND_SIZE 13
//game status constants
#define WAITING 0
#define READY_TO_START 1
#define IN_GAME 2
#define GAME_OVER 3

typedef struct Game Game;

// a Mahjong tile with a category (D, B, C, W, or G) and a value (1-9, n/s/e/w, or r/g/b)
typedef struct {
    char category;
    char value;
} Tile;

// a player in the game with a player number from 1-4 and a hand consisting of up to 14 Mahjong tiles
typedef struct {
    int player_number;
    int socket; // the client socket associated with this player
    Tile hand[MAX_HAND_SIZE];
    Tile revealed[MAX_HAND_SIZE];
    Game *game; // pointer to the game the player is in, or NULL if not in game yet
} Player;

// represents information about the current game, including the players, tiles, the player whose turn it is, and the index in the list of tiles at which the next tile will be drawn
struct Game {
    Player players[PLAYER_NUM];
    Tile tiles[TILE_NUM];
    Tile discard[TILE_NUM];
    int curr_player; // index in the players list of the current player. curr_player+1 is that player's player_number.
    int curr_tile;
    int num_players;
    int game_status;
    int discard_count;
    char responses[PLAYER_NUM];
    Tile tile_responses1[2];
    Tile tile_responses2[2];
};

// functions related to an instance of a mahjong game
int init_game(Game *game);
Player *add_player(Game *game, int soc);

// client side player struct
typedef struct {
    int player_number;
    Tile hand[MAX_HAND_SIZE];
    Tile revealed[MAX_HAND_SIZE];
} ClientPlayer;

// client side game struct
typedef struct {
    ClientPlayer players[PLAYER_NUM];
    int players_index; // index of the player that the client represents in the players array
    Tile discard[TILE_NUM];
    int curr_player;
    int num_players;
    int game_status;
    int discard_count;
} ClientGame;

int init_client_game(ClientGame *game);

#endif
