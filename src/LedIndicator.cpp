#include "../include/LedIndicator.hpp"

#include <unistd.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>

namespace gsr {
    LedIndicator::LedIndicator() {
        run_gsr_global_hotkeys_set_leds(false);
    }

    LedIndicator::~LedIndicator() {
        run_gsr_global_hotkeys_set_leds(false);
        if(gsr_global_hotkeys_pid > 0) {
            int status;
            waitpid(gsr_global_hotkeys_pid, &status, 0);
        }
    }

    void LedIndicator::set_led(bool enabled) {
        led_enabled = enabled;
        perform_blink = false;
    }

    void LedIndicator::blink() {
        perform_blink = true;
        blink_timer.restart();
    }

    bool LedIndicator::run_gsr_global_hotkeys_set_leds(bool enabled) {
        if(gsr_global_hotkeys_pid > 0) {
            int status;
            if(waitpid(gsr_global_hotkeys_pid, &status, WNOHANG) == 0) {
                // Still running
                return false;
            }
            gsr_global_hotkeys_pid = -1;
        }

        const bool inside_flatpak = getenv("FLATPAK_ID") != NULL;
        const char *user_homepath = getenv("HOME");
        if(!user_homepath)
            user_homepath = "/tmp";

        gsr_global_hotkeys_pid = vfork();
        if(gsr_global_hotkeys_pid == -1) {
            fprintf(stderr, "Error: LedIndicator::run_gsr_global_hotkeys_set_leds: failed to fork\n");
            return false;
        } else if(gsr_global_hotkeys_pid == 0) { // Child
            if(inside_flatpak) {
                const char *args[] = { "flatpak-spawn", "--host", "/var/lib/flatpak/app/com.dec05eba.gpu_screen_recorder/current/active/files/bin/kms-server-proxy", "launch-gsr-global-hotkeys", user_homepath, "--set-led", "Scroll Lock", enabled ? "on" : "off", nullptr };
                execvp(args[0], (char* const*)args);
            } else {
                const char *args[] = { "gsr-global-hotkeys", "--set-led", "Scroll Lock", enabled ? "on" : "off", nullptr };
                execvp(args[0], (char* const*)args);
            }

            perror("gsr-global-hotkeys");
            _exit(127);
            return true;
        } else { // Parent
            return true;
        }
    }

    void LedIndicator::update_led(bool new_state) {
        if(new_state == led_indicator_on)
            return;

        if(run_gsr_global_hotkeys_set_leds(new_state))
            led_indicator_on = new_state;
    }

    void LedIndicator::update() {
        if(perform_blink) {
            const double blink_elapsed_sec = blink_timer.get_elapsed_time_seconds();
            if(blink_elapsed_sec < 0.2) {
                update_led(false);
            } else if(blink_elapsed_sec < 0.4) {
                update_led(true);
            } else if(blink_elapsed_sec < 0.6) {
                update_led(false);
            } else if(blink_elapsed_sec < 0.8) {
                update_led(true);
            } else {
                perform_blink = false;
            }
        } else {
            update_led(led_enabled);
        }
    }
}