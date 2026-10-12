#!/usr/bin/env python3

import sys
import os
import json

# Game names parsed from the ananicy comments that are descriptions instead of names
game_name_overrides = {
    "'0 A.D.' RTS": "0 A.D.",
    "Football Manager Series": "Football Manager",
    "Ironwail: High-performance QuakeSpasm fork": "Ironwail",
    "Manicraft Bedrock Launcher": "Minecraft Bedrock",
    "OpenMoHAA: An open-source game engine compatible with Medal of Honor Allied Assault and its expansions": "OpenMoHAA",
    "PA UI": "Planetary Annihilation",
    "Sober": "Roblox",
    "SuperTux Platformer": "SuperTux",
    "Unreal Gold: OldUnreal Linux port": "Unreal Gold",
    "Unreal Tournament 99: OldUnreal Linux port of UT GOTY": "Unreal Tournament 99",
}

blacklisted_process_names = ("lsfg-vk-ui ",)

def parse_game_name_from_comment(comment):
    words = [word for word in comment.lstrip("#").split() if "://" not in word]
    game_name = " ".join(words)
    for separator in (" : ", " is a ", " is an ", ", ", "; "):
        game_name = game_name.split(separator)[0]
    game_name = game_name.strip(" :.,;")
    return game_name_overrides.get(game_name, game_name)

# Skips steam games and games that end with .x86_64, .x86 or .x64.
# Returns a list of (process_name, game_name). The game name is empty if |include_game_names| is false or if the game doesn't have a comment
def extract_non_tracked_games(filepath, include_game_names):
    games = []
    with open(filepath, "r") as file:
        is_steam_game = False
        game_name = ""
        for line in file:
            if line.startswith("##"):
                continue
            elif line.startswith("#"):
                if "steam" in line:
                    is_steam_game = True
                game_name = parse_game_name_from_comment(line) if include_game_names else ""
            elif line.startswith("{"):
                if is_steam_game:
                    continue

                app_info = json.loads(line)
                if app_info["type"] != "Game":
                    continue

                process_name = app_info["name"]
                if not process_name.endswith(".x86_64") and not process_name.endswith(".x86") and not process_name.endswith(".x64"):
                    games.append((process_name, game_name))
            else:
                is_steam_game = False
                game_name = ""
    return games

def c_string_literal(s):
    return '"%s"' % s.replace("\\", "\\\\").replace('"', '\\"').replace("?", "\\?")

def write_process_name_matcher_code_file(filepath, all_games):
    games_by_len = {}
    for process_name, game_name in all_games:
        games_by_len.setdefault(len(process_name), []).append((process_name, game_name))

    games_by_len = dict(sorted(games_by_len.items()))

    with open(filepath, "w") as file:
        file.write(
            '#include "native_games.h"\n'
            '\n'
            '#include <string.h>\n'
            '\n'
            '/* This file was auto-generated with create-native-games-list.py, do not edit manually! */\n'
            '\n'
            'typedef struct {\n'
            '    const char *process_name;\n'
            '    const char *game_name;\n'
            '} NativeGame;\n'
            '\n')

        for game_len in games_by_len:
            games = ", ".join("{ %s, %s }" % (c_string_literal(process_name), c_string_literal(game_name)) for process_name, game_name in games_by_len[game_len])
            file.write("static const NativeGame native_games_len_%d[] = { %s, { NULL, NULL } };\n" % (game_len, games))

        file.write(
            "\n"
            "static const char* find_native_game_name(const NativeGame *native_games, const char *process_name, size_t size) {\n"
            "    for(size_t i = 0; native_games[i].process_name != NULL; ++i) {\n"
            "        if(memcmp(process_name, native_games[i].process_name, size) == 0)\n"
            "            return native_games[i].game_name;\n"
            "    }\n"
            "    return NULL;\n"
            "}\n"
            "\n"
            "const char* get_native_game_name(const char *process_name, size_t size) {\n"
            "    switch(size) {\n")

        for game_len in games_by_len:
            file.write("        case %d: return find_native_game_name(native_games_len_%d, process_name, size);\n" % (game_len, game_len))

        file.write(
            "        default: return NULL;\n"
            "    }\n"
            "}\n"
        )

def add_games_from_rules_filepath(filepath, include_game_names):
    games = extract_non_tracked_games(filepath, include_game_names)
    print("Scanning file: %s, num games found: %d" % (filepath, len(games)))
    return games

def add_games_from_rules_recursive(dir, include_game_names):
    games_added = []
    for folder, subfolders, files in os.walk(dir):
        for file in files:
            filepath = os.path.abspath(os.path.join(folder, file))
            games_added.extend(add_games_from_rules_filepath(filepath, include_game_names))
    return games_added

def main():
    if len(sys.argv) != 2:
        print("usage: create-native-games-list.py <path-to-ananicy-rules-git-repo>")
        exit(1)

    ananicy_root_path = sys.argv[1]
    ananicy_conf_path = os.path.join(ananicy_root_path, "ananicy.conf")
    if not os.path.exists(ananicy_conf_path):
        print("error: the path this script was launched with is not the ananicy-rules root directory (file %s doesn't exist)" % ananicy_conf_path)
        exit(1)

    ananicy_native_games_path = os.path.join(ananicy_root_path, "00-default", "Games", "linux-native")
    ananicy_emulators_path = os.path.join(ananicy_root_path, "00-default", "Games", "emulators.rules")
    ananicy_launchers_path = os.path.join(ananicy_root_path, "00-default", "Games", "launchers.rules")
    ananicy_waydroid_path = os.path.join(ananicy_root_path, "00-default", "Games", "waydroid.rules")

    # Emulators, launchers and game streaming clients don't have a game name. The window title is used instead (by gsr-ui)
    all_games = []
    all_games.extend(add_games_from_rules_recursive(ananicy_native_games_path, True))
    all_games.extend(add_games_from_rules_filepath(ananicy_emulators_path, False))
    all_games.extend(add_games_from_rules_filepath(ananicy_launchers_path, False))
    all_games.extend(add_games_from_rules_filepath(ananicy_waydroid_path, True))
    all_games.append(("supertux2", "SuperTux"))
    all_games.append(("etr", "Extreme Tux Racer"))
    all_games.append(("TETR.IO", "TETR.IO"))
    all_games.append(("parsecd", ""))
    all_games.append(("moonlight", ""))
    all_games.append(("moonlight-qt", ""))

    games_by_process_name = {}
    for process_name, game_name in all_games:
        if process_name not in blacklisted_process_names and process_name not in games_by_process_name:
            games_by_process_name[process_name] = game_name

    script_dir = os.path.dirname(os.path.realpath(__file__))
    write_process_name_matcher_code_file(os.path.join(script_dir, "native_games.c"), games_by_process_name.items())

main()
