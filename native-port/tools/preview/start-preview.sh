#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"
port="${OPENSIM_PREVIEW_PORT:-19093}"
bind="${OPENSIM_PREVIEW_BIND:-127.0.0.1}"
if [[ ! "$port" =~ ^[0-9]+$ ]] || (( port < 1 || port > 65535 )); then
    echo 'OPENSIM_PREVIEW_PORT must be between 1 and 65535' >&2
    exit 1
fi
if [[ "$bind" != 127.0.0.1 && "$bind" != 0.0.0.0 ]]; then
    echo 'OPENSIM_PREVIEW_BIND must be 127.0.0.1 or 0.0.0.0' >&2
    exit 1
fi
mkdir -p data
if [[ ! -f preview.ini ]]; then
    if [[ ! -f data/preview.ini ]]; then
        sed -e "s/%PORT%/$port/g" -e "s/%BIND%/$bind/g" share/opensim-native/preview.ini.template > data/preview.ini
    fi
    ln -s data/preview.ini preview.ini
fi
configured_port=$(sed -n 's/^http_listener_port[[:space:]]*=[[:space:]]*\([0-9]*\)[[:space:]]*$/\1/p' preview.ini)
if [[ "$configured_port" != "$port" ]]; then
    echo 'Existing preview.ini has a different port; reuse that port to retain its settings' >&2
    exit 1
fi
if [[ ! -f data/users.db ]]; then
    bin/opensim-native --config preview.ini --init-db
fi
password="${OPENSIM_PREVIEW_PASSWORD:-preview-password}"
for last in First Second; do
    marker="data/account-${last,,}.txt"
    [[ ! -f "$marker" ]] || continue
    if output=$(printf '%s\n' "$password" | bin/opensim-native --config preview.ini --create-user Preview "$last" 2>&1); then
        printf '%s\n' "$output" > "$marker"
    elif [[ "$output" == *'User already exists'* ]]; then
        printf '%s\n' 'Existing account retained' > "$marker"
    else
        printf '%s\n' "$output" >&2
        exit 1
    fi
done
printf 'Preview grid: http://127.0.0.1:%s/\n' "$port"
printf '%s\n' 'Accounts: Preview First / Preview Second. Initial test password: preview-password (or OPENSIM_PREVIEW_PASSWORD).'
printf '%s\n' 'Type quit to stop cleanly; data and preview.ini remain for the next launch.'
exec bin/opensim-native --config preview.ini --console
