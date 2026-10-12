#ifndef NATIVE_GAMES_H
#define NATIVE_GAMES_H

#include <stddef.h>

/*
    Checks for native games (or emulators) that are not steam games and neither ends with .x86_64, .x86 or .x64.
    Returns NULL if the process is not a native game, otherwise the name of the game (an empty string if the name is not known).
*/
const char* get_native_game_name(const char *process_name, size_t size);

#endif /* NATIVE_GAMES_H */
