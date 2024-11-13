#!/bin/sh

[ "$GSR_SHOW_SAVED_NOTIFICATION" != "1" ] && exit 0

filepath="$1"
type="$2"

file_name="$(basename "$filepath")"

case "$type" in
    "regular")
        gsr-notify --text "Saved recording to '$file_name'" --timeout 3.0 --icon record --bg-color "$GSR_NOTIFY_BG_COLOR"
        ;;
    "replay")
        gsr-notify --text "Saved replay to '$file_name'" --timeout 3.0 --icon replay --bg-color "$GSR_NOTIFY_BG_COLOR"
        ;;
esac