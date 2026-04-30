#include "native_games.h"

#include <string.h>

/* This file was auto-generated with create-native-games-list.py, do not edit manually! */

static const char *process_names_len_12[] = { "soh.appimage", "supertuxkart", "HytaleClient", "Vintagestory", "ut-bin-amd64", "fs-uae-devel", "xenia_canary", "shadPS4:Main", NULL };
static const char *process_names_len_5[] = { "sober", "zdoom", "cen64", "mesen", "higan", "rpcs3", NULL };
static const char *process_names_len_14[] = { "sober_services", "chocolate-doom", "CodenameEngine", "quakespasm-svn", "2ship.appimage", "rott-shareware", "unreal-bin-x86", "PPSSPPHeadless", NULL };
static const char *process_names_len_15[] = { "UnleashedRecomp", "CoherentUI_Host", "linux_64_client", "rott-registered", "UnrealLinux.bin", "Quake3-UrT.i386", "DuckStation-x64", "tarEN.Arknights", "xon.bluearchive", "o.GenshinImpact", "iHoYo.bh3global", "se.hkrpgoversea", "ng.grayraven.en", ".en.reverse1999", "ingwaves.global", "m.HoYoverse.Nap", NULL };
static const char *process_names_len_10[] = { "sm64coopdx", "luanti.bin", "quakespasm", "srcds_i486", "rbdoom3bfg", "tyr-glqwcl", "ut-bin-x86", NULL };
static const char *process_names_len_6[] = { "luanti", "Mobbos", "glhwcl", "hexen2", "dhewm3", "Funkin", "quake2", "gzdoom", "quake3", "ecwolf", "ut-bin", "fs-uae", "dosbox", NULL };
static const char *process_names_len_11[] = { "PsychEngine", "devilutionx", "d1x-rebirth", "d2x-rebirth", "Kade Engine", "tyr-glquake", "xonotic-glx", "xonotic-sdl", "dolphin-emu", NULL };
static const char *process_names_len_8[] = { "glhexen2", "alephone", "openrct2", "freesynd", "rottexpr", "tyr-qwcl", "ioquake3", "ironwail", "vkquake2", "cen64-qt", "mednafen", "PPSSPPQt", "pcsx2-qt", NULL };
static const char *process_names_len_4[] = { "hwcl", "osu!", "rott", "xemu", "cemu", "suyu", "yuzu", "eden", "mame", NULL };
static const char *process_names_len_9[] = { "openmohaa", "omohaaded", "berkelium", "cefsimple", "srcds_run", "tyr-quake", "iortcw-mp", "iortcw-sp", "xenia.exe", "retroarch", "mesen-git", "PPSSPPSDL", "supertux2", NULL };
static const char *process_names_len_21[] = { "launch_openmohaa_base", NULL };
static const char *process_names_len_29[] = { "launch_openmohaa_breakthrough", NULL };
static const char *process_names_len_26[] = { "launch_openmohaa_spearhead", NULL };
static const char *process_names_len_13[] = { "yamagi-quake2", "prismlauncher", NULL };
static const char *process_names_len_17[] = { "yamagi-quake2-git", "Zelda64Recompiled", "xonotic-local-sdl", NULL };
static const char *process_names_len_3[] = { "0ad", "etl", "RMG", "etr", NULL };
static const char *process_names_len_2[] = { "fm", "et", NULL };
static const char *process_names_len_7[] = { "Etterna", "etterna", "vkquake", "melonDS", "ryujinx", "sdlmame", "scummvm", "blastem", "redream", "shadps4", "TETR.IO", NULL };
static const char *process_names_len_16[] = { "unreal-bin-amd64", "xenia_canary.exe", "elyprismlauncher", NULL };

bool is_process_name_native_game(const char *process_name, size_t size) {
    switch(size) {
        case 12: {
            for(size_t i = 0; process_names_len_12[i] != NULL; ++i) {
                if(memcmp(process_name, process_names_len_12[i], size) == 0)
                    return true;
            }
            return false;
        }
        case 5: {
            for(size_t i = 0; process_names_len_5[i] != NULL; ++i) {
                if(memcmp(process_name, process_names_len_5[i], size) == 0)
                    return true;
            }
            return false;
        }
        case 14: {
            for(size_t i = 0; process_names_len_14[i] != NULL; ++i) {
                if(memcmp(process_name, process_names_len_14[i], size) == 0)
                    return true;
            }
            return false;
        }
        case 15: {
            for(size_t i = 0; process_names_len_15[i] != NULL; ++i) {
                if(memcmp(process_name, process_names_len_15[i], size) == 0)
                    return true;
            }
            return false;
        }
        case 10: {
            for(size_t i = 0; process_names_len_10[i] != NULL; ++i) {
                if(memcmp(process_name, process_names_len_10[i], size) == 0)
                    return true;
            }
            return false;
        }
        case 6: {
            for(size_t i = 0; process_names_len_6[i] != NULL; ++i) {
                if(memcmp(process_name, process_names_len_6[i], size) == 0)
                    return true;
            }
            return false;
        }
        case 11: {
            for(size_t i = 0; process_names_len_11[i] != NULL; ++i) {
                if(memcmp(process_name, process_names_len_11[i], size) == 0)
                    return true;
            }
            return false;
        }
        case 8: {
            for(size_t i = 0; process_names_len_8[i] != NULL; ++i) {
                if(memcmp(process_name, process_names_len_8[i], size) == 0)
                    return true;
            }
            return false;
        }
        case 4: {
            for(size_t i = 0; process_names_len_4[i] != NULL; ++i) {
                if(memcmp(process_name, process_names_len_4[i], size) == 0)
                    return true;
            }
            return false;
        }
        case 9: {
            for(size_t i = 0; process_names_len_9[i] != NULL; ++i) {
                if(memcmp(process_name, process_names_len_9[i], size) == 0)
                    return true;
            }
            return false;
        }
        case 21: {
            for(size_t i = 0; process_names_len_21[i] != NULL; ++i) {
                if(memcmp(process_name, process_names_len_21[i], size) == 0)
                    return true;
            }
            return false;
        }
        case 29: {
            for(size_t i = 0; process_names_len_29[i] != NULL; ++i) {
                if(memcmp(process_name, process_names_len_29[i], size) == 0)
                    return true;
            }
            return false;
        }
        case 26: {
            for(size_t i = 0; process_names_len_26[i] != NULL; ++i) {
                if(memcmp(process_name, process_names_len_26[i], size) == 0)
                    return true;
            }
            return false;
        }
        case 13: {
            for(size_t i = 0; process_names_len_13[i] != NULL; ++i) {
                if(memcmp(process_name, process_names_len_13[i], size) == 0)
                    return true;
            }
            return false;
        }
        case 17: {
            for(size_t i = 0; process_names_len_17[i] != NULL; ++i) {
                if(memcmp(process_name, process_names_len_17[i], size) == 0)
                    return true;
            }
            return false;
        }
        case 3: {
            for(size_t i = 0; process_names_len_3[i] != NULL; ++i) {
                if(memcmp(process_name, process_names_len_3[i], size) == 0)
                    return true;
            }
            return false;
        }
        case 2: {
            for(size_t i = 0; process_names_len_2[i] != NULL; ++i) {
                if(memcmp(process_name, process_names_len_2[i], size) == 0)
                    return true;
            }
            return false;
        }
        case 7: {
            for(size_t i = 0; process_names_len_7[i] != NULL; ++i) {
                if(memcmp(process_name, process_names_len_7[i], size) == 0)
                    return true;
            }
            return false;
        }
        case 16: {
            for(size_t i = 0; process_names_len_16[i] != NULL; ++i) {
                if(memcmp(process_name, process_names_len_16[i], size) == 0)
                    return true;
            }
            return false;
        }
        default: {
            return false;
        }
    }
}
