#include "../include/Translation.hpp"
#include <cstring>
#include <unordered_map>
#include <fstream>

namespace gsr {
    std::string Translation::get_system_language() {
        const char* lang = getenv("LANGUAGE");
        if (!lang || !lang[0]) lang = getenv("LC_ALL");
        if (!lang || !lang[0]) lang = getenv("LC_MESSAGES");
        if (!lang || !lang[0]) lang = getenv("LANG");

        if (lang && lang[0]) {
            std::string lang_str(lang);

            // we usually need only two symbols
            size_t underscore = lang_str.find('_');
            if (underscore != std::string::npos) {
                return lang_str.substr(0, underscore);
            }
            size_t dot = lang_str.find('.');
            if (dot != std::string::npos) {
                return lang_str.substr(0, dot);
            }
            return lang_str;
        }

        return "en";
    }

    std::string Translation::trim(const std::string& str) {
        size_t start = str.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) return "";
        size_t end = str.find_last_not_of(" \t\r\n");
        return str.substr(start, end - start + 1);
    }

    bool Translation::load_language(const char* lang) {
        translations.clear();
        current_language = lang;

        if (strcmp(lang, "en") == 0) {
            return true; // english is the base
        }

        std::string paths[] = {
            std::string("translations/") + lang + ".txt",
            std::string(this->translations_directory) + lang + ".txt"
        };

        for (const auto& path : paths) {
            std::ifstream file(path);
            if (!file.is_open()) continue;

            std::string line;
            while (std::getline(file, line)) {
                line = trim(line);

                if (line.empty() || line[0] == '#') continue;

                size_t eq_pos = line.find('=');
                if (eq_pos == std::string::npos) continue;

                std::string key = trim(line.substr(0, eq_pos));
                std::string value = trim(line.substr(eq_pos + 1));

                size_t pos = 0;
                while ((pos = value.find("\\n", pos)) != std::string::npos) {
                    value.replace(pos, 2, "\n");
                    pos += 1;
                }

                translations[key] = value;
            }

            return true;
        }

        fprintf(stderr, "Warning: translation file for '%s' not found\n", lang);
        return false;
    }

    Translation& Translation::instance() {
        static Translation tr;
        return tr;
    }

    void Translation::init(const char* translations_directory, const char* initial_language) {
        this->translations_directory = translations_directory;

        load_language(initial_language == nullptr ? get_system_language().c_str() : initial_language);
    }

    const char* Translation::translate(const char* key) {
        auto it = translations.find(key);
        if (it != translations.end()) {
            return it->second.c_str();
        }
        return key; // falling back if nothing found
    }
}