#!/bin/bash
# rezoom-notify-hook — Claude Code Notification hook feeding Rezoom's freeze
# detection. Appends {ts, session_id, message, title} to
# ~/.local/share/rezoom/notifications.jsonl (or $REZOOM_DATA_DIR).
#
# Wire it into ~/.claude/settings.json:
#   "hooks": { "Notification": [ { "hooks": [
#     { "type": "command", "command": "rezoom-notify-hook", "async": true } ] } ] }
input=$(cat)
dir="${REZOOM_DATA_DIR:-$HOME/.local/share/rezoom}"
mkdir -p "$dir"
jq -c '{ts: (now * 1000 | floor), session_id: (.session_id // ""),
      message: (.message // ""), title: (.title // "")}' \
    <<< "$input" >> "$dir/notifications.jsonl" 2>/dev/null || true
