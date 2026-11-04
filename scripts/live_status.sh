#!/bin/bash
# live_status.sh - the live loop's 'status' (docs/re-xr-pipeline.md 162): python3 -I live.py status "$@" (see live.py's header)
exec python3 -I "$(cd "$(dirname "$0")" && pwd)/live.py" status "$@"
