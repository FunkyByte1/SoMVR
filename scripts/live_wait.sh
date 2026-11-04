#!/bin/bash
# live_wait.sh - the live loop's 'wait' (docs/re-xr-pipeline.md 162): python3 -I live.py wait "$@" (see live.py's header)
exec python3 -I "$(cd "$(dirname "$0")" && pwd)/live.py" wait "$@"
