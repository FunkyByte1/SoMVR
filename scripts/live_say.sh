#!/bin/bash
# live_say.sh - the live loop's 'say' (docs/re-xr-pipeline.md 162): python3 -I live.py say "$@" (see live.py's header)
exec python3 -I "$(cd "$(dirname "$0")" && pwd)/live.py" say "$@"
