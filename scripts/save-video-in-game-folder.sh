#!/bin/sh

filepath="$1"
type="$2"

file_name="$(basename "$filepath")"
file_dir="$(dirname "$filepath")"

game_name=$(gsr-window-name focused || echo "Game")
target_dir="$file_dir/$game_name"
new_filepath="$target_dir/$file_name"

mkdir -p "$target_dir"
mv "$filepath" "$new_filepath"

[ "$GSR_SHOW_SAVED_NOTIFICATION" != "1" ] && exit 0

case "$type" in
    "regular")
        gsr-notify --text "Saved recording to a folder called '$game_name'" --timeout 3.0 --icon record --bg-color "$GSR_NOTIFY_BG_COLOR"
        ;;
    "replay")
        gsr-notify --text "Saved replay to a folder called '$game_name'" --timeout 3.0 --icon replay --bg-color "$GSR_NOTIFY_BG_COLOR"
        ;;
esac