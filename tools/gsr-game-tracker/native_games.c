#include "native_games.h"

#include <string.h>

/* This file was auto-generated with create-native-games-list.py, do not edit manually! */

typedef struct {
    const char *process_name;
    const char *game_name;
} NativeGame;

static const NativeGame native_games_len_2[] = { { "et", "etlegacy" }, { NULL, NULL } };
static const NativeGame native_games_len_3[] = { { "0ad", "0 A.D." }, { "KSA", "Kitten Space Agency" }, { "etl", "etlegacy" }, { "RMG", "" }, { "etr", "Extreme Tux Racer" }, { NULL, NULL } };
static const NativeGame native_games_len_4[] = { { "hwcl", "Hexen II: Hammer of Thyrion" }, { "osu!", "osu! lazer" }, { "rott", "rottexptr" }, { "xemu", "" }, { "cemu", "" }, { "suyu", "" }, { "yuzu", "" }, { "eden", "" }, { "mame", "" }, { NULL, NULL } };
static const NativeGame native_games_len_5[] = { { "sober", "Roblox" }, { "zdoom", "zdoom" }, { "bsnes", "" }, { "cen64", "" }, { "mesen", "" }, { "higan", "" }, { "86Box", "" }, { "PCBox", "" }, { "rpcs3", "" }, { NULL, NULL } };
static const NativeGame native_games_len_6[] = { { "luanti", "Luanti" }, { "Mobbos", "Mobmod" }, { "glhwcl", "Hexen II: Hammer of Thyrion" }, { "hexen2", "Hexen II: Hammer of Thyrion" }, { "dhewm3", "dhewm3" }, { "Funkin", "Funkin" }, { "quake2", "yamagi-quake2" }, { "gzdoom", "zdoom" }, { "uzdoom", "zdoom" }, { "taisei", "Taisei Project" }, { "quake3", "ioquake3" }, { "ecwolf", "ECWolf" }, { "ut-bin", "Unreal Tournament 99" }, { "fs-uae", "" }, { "ruffle", "" }, { "dosbox", "" }, { NULL, NULL } };
static const NativeGame native_games_len_7[] = { { "Etterna", "etterna" }, { "etterna", "etterna" }, { "vkquake", "vkquake" }, { "melonDS", "" }, { "ryujinx", "" }, { "sdlmame", "" }, { "scummvm", "" }, { "blastem", "" }, { "redream", "" }, { "shadps4", "" }, { "TETR.IO", "TETR.IO" }, { "parsecd", "" }, { NULL, NULL } };
static const NativeGame native_games_len_8[] = { { "mocktail", "Mocktail" }, { "glhexen2", "Hexen II: Hammer of Thyrion" }, { "openrct2", "openrct2" }, { "freesynd", "freesynd" }, { "rottexpr", "rottexptr" }, { "rs3linux", "RuneScape 3" }, { "tyr-qwcl", "tyrquake" }, { "ioquake3", "ioquake3" }, { "ironwail", "Ironwail" }, { "vkquake2", "vkquake2" }, { "cen64-qt", "" }, { "mednafen", "" }, { "PPSSPPQt", "" }, { "pcsx2-qt", "" }, { NULL, NULL } };
static const NativeGame native_games_len_9[] = { { "supertux2", "SuperTux" }, { "openmohaa", "OpenMoHAA" }, { "omohaaded", "OpenMoHAA" }, { "berkelium", "Football Manager" }, { "cefsimple", "Football Manager" }, { "rs2client", "RuneScape 3" }, { "tyr-quake", "tyrquake" }, { "iortcw-mp", "Iortcw" }, { "iortcw-sp", "Iortcw" }, { "xenia.exe", "" }, { "retroarch", "" }, { "mesen-git", "" }, { "PPSSPPSDL", "" }, { "moonlight", "" }, { NULL, NULL } };
static const NativeGame native_games_len_10[] = { { "sm64coopdx", "Super Mario 64 Coop DX" }, { "luanti.bin", "Luanti" }, { "quakespasm", "quakespasm" }, { "rbdoom3bfg", "rbdoom3bfg" }, { "tyr-glqwcl", "tyrquake" }, { "ut-bin-x86", "Unreal Tournament 99" }, { NULL, NULL } };
static const NativeGame native_games_len_11[] = { { "PsychEngine", "Psych Engine" }, { "warzone2100", "Warzone 2100" }, { "devilutionx", "devilutionx" }, { "d1x-rebirth", "dxx rebirth" }, { "d2x-rebirth", "dxx rebirth" }, { "Kade Engine", "Kade Engine" }, { "tyr-glquake", "tyrquake" }, { "xonotic-glx", "Xonotic" }, { "xonotic-sdl", "Xonotic" }, { "dolphin-emu", "" }, { NULL, NULL } };
static const NativeGame native_games_len_12[] = { { "soh.appimage", "Ship of Harkinian" }, { "supertuxkart", "SuperTuxKart" }, { "HytaleClient", "Hytale" }, { "Vintagestory", "Vintage Story" }, { "ut-bin-amd64", "Unreal Tournament 99" }, { "fs-uae-devel", "" }, { "xenia_canary", "" }, { "shadPS4:Main", "" }, { "moonlight-qt", "" }, { NULL, NULL } };
static const NativeGame native_games_len_13[] = { { "yamagi-quake2", "yamagi-quake2" }, { "prismlauncher", "" }, { "fjordlauncher", "" }, { NULL, NULL } };
static const NativeGame native_games_len_14[] = { { "sober_services", "Roblox" }, { "chocolate-doom", "Chocolate doom" }, { "CodenameEngine", "Codename Engine" }, { "quakespasm-svn", "quakespasm" }, { "2ship.appimage", "2 Ship 2 Harkinian" }, { "rott-shareware", "rott" }, { "unreal-bin-x86", "Unreal Gold" }, { "easyrpg-player", "" }, { "PPSSPPHeadless", "" }, { NULL, NULL } };
static const NativeGame native_games_len_15[] = { { "UnleashedRecomp", "Sonic Unleashed Recompiled" }, { "CoherentUI_Host", "Planetary Annihilation" }, { "linux_64_client", "AssaultCube" }, { "rott-registered", "rott" }, { "UnrealLinux.bin", "unreal 98" }, { "Quake3-UrT.i386", "Urban Terror" }, { "DuckStation-x64", "" }, { "tarEN.Arknights", "Arknights" }, { "xon.bluearchive", "Blue Archive" }, { "o.GenshinImpact", "Genshin Impact" }, { "iHoYo.bh3global", "Honkai Impact 3rd" }, { "se.hkrpgoversea", "Honkai: Star Rail" }, { "ng.grayraven.en", "Punishing Gray Raven" }, { ".en.reverse1999", "Reverse: 1999" }, { "ingwaves.global", "Wuthering Waves" }, { "m.HoYoverse.Nap", "Zenless Zone Zero" }, { NULL, NULL } };
static const NativeGame native_games_len_16[] = { { "unreal-bin-amd64", "Unreal Gold" }, { "xenia_canary.exe", "" }, { "elyprismlauncher", "" }, { NULL, NULL } };
static const NativeGame native_games_len_17[] = { { "yamagi-quake2-git", "yamagi-quake2" }, { "Zelda64Recompiled", "The Legend of Zelda: Majora's Mask Recompiled" }, { "xonotic-local-sdl", "Xonotic" }, { NULL, NULL } };
static const NativeGame native_games_len_18[] = { { "mcpelauncher-ui-qt", "Minecraft Bedrock" }, { NULL, NULL } };
static const NativeGame native_games_len_19[] = { { "mcpelauncher-client", "Minecraft Bedrock" }, { NULL, NULL } };
static const NativeGame native_games_len_21[] = { { "launch_openmohaa_base", "OpenMoHAA" }, { NULL, NULL } };
static const NativeGame native_games_len_26[] = { { "launch_openmohaa_spearhead", "OpenMoHAA" }, { NULL, NULL } };
static const NativeGame native_games_len_29[] = { { "launch_openmohaa_breakthrough", "OpenMoHAA" }, { NULL, NULL } };

static const char* find_native_game_name(const NativeGame *native_games, const char *process_name, size_t size) {
    for(size_t i = 0; native_games[i].process_name != NULL; ++i) {
        if(memcmp(process_name, native_games[i].process_name, size) == 0)
            return native_games[i].game_name;
    }
    return NULL;
}

const char* get_native_game_name(const char *process_name, size_t size) {
    switch(size) {
        case 2: return find_native_game_name(native_games_len_2, process_name, size);
        case 3: return find_native_game_name(native_games_len_3, process_name, size);
        case 4: return find_native_game_name(native_games_len_4, process_name, size);
        case 5: return find_native_game_name(native_games_len_5, process_name, size);
        case 6: return find_native_game_name(native_games_len_6, process_name, size);
        case 7: return find_native_game_name(native_games_len_7, process_name, size);
        case 8: return find_native_game_name(native_games_len_8, process_name, size);
        case 9: return find_native_game_name(native_games_len_9, process_name, size);
        case 10: return find_native_game_name(native_games_len_10, process_name, size);
        case 11: return find_native_game_name(native_games_len_11, process_name, size);
        case 12: return find_native_game_name(native_games_len_12, process_name, size);
        case 13: return find_native_game_name(native_games_len_13, process_name, size);
        case 14: return find_native_game_name(native_games_len_14, process_name, size);
        case 15: return find_native_game_name(native_games_len_15, process_name, size);
        case 16: return find_native_game_name(native_games_len_16, process_name, size);
        case 17: return find_native_game_name(native_games_len_17, process_name, size);
        case 18: return find_native_game_name(native_games_len_18, process_name, size);
        case 19: return find_native_game_name(native_games_len_19, process_name, size);
        case 21: return find_native_game_name(native_games_len_21, process_name, size);
        case 26: return find_native_game_name(native_games_len_26, process_name, size);
        case 29: return find_native_game_name(native_games_len_29, process_name, size);
        default: return NULL;
    }
}
