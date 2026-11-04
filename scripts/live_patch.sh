#!/bin/bash
# live_patch.sh - the live loop's 'patch' (docs/re-xr-pipeline.md 162): python3 -I live.py patch "$@" (see live.py's header)
exec python3 -I "$(cd "$(dirname "$0")" && pwd)/live.py" patch "$@"
