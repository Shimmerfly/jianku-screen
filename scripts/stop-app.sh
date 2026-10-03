#!/bin/sh
# Stops the app without killing the shell that runs this script.
#
# The obvious `ps | grep '[J]ianku Screen.app/Contents/MacOS' | xargs kill` kills the
# invoking shell too: the shell's own command line contains the path it is about to
# launch, so it matches its own pattern. Matching the executable by absolute path and
# excluding self and parents is what actually works.
set -e
me=$$
myparent=$(ps -o ppid= -p "$me" | tr -d ' ')
ps -eo pid=,ppid=,comm= | while read -r pid ppid comm; do
    [ "$pid" = "$me" ] && continue
    [ "$pid" = "$myparent" ] && continue
    case "$comm" in
        *"Jianku Screen"*) echo "$pid" ;;
    esac
done | xargs -r kill 2>/dev/null || true
exit 0
