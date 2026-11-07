#!/bin/sh
# Rebuilds the presentation video from scratch: scenarios, modulator voice, simulator runs, audio, picture, three mp4 files.
# usage: tools/video/make_video.sh [--work DIR] [--out DIR] [--stage plan|voices|sim|mix|frames|encode|all]
exec python3 "$(dirname "$0")/make_video.py" "$@"
