#ifndef GAME_NAME_H
#define GAME_NAME_H

#include <stddef.h>
#include <stdbool.h>

/* Returns true if app_id is a steam app/game id */
bool is_valid_app_id(const char *app_id);

/* Resolves the steam name of the game into |name|. |name| is set to an empty string if the game isn't a steam game or the name couldn't be found */
void game_name_resolve(const char *app_id, const char *steam_base_folder, char *name, size_t name_size);

#endif /* GAME_NAME_H */
