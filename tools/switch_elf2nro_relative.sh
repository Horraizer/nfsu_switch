#!/usr/bin/env bash
# Optional MSYS2 packaging adapter for a non-ASCII workspace. The SDK's
# Windows RomFS reader accepts relative ASCII names but rejects the absolute
# workspace argument. Keep the real build cwd and the original SDK packager.
set -euo pipefail
args=()
for arg in "$@"; do
    case "$arg" in
        --romfsdir=*|--nacp=*)
            prefix="${arg%%=*}"
            source="${arg#*=}"
            args+=("$prefix=$(realpath --relative-to="$PWD" "$source")")
            ;;
        /*.elf|/*.nro)
            args+=("$(realpath --relative-to="$PWD" "$arg")")
            ;;
        *) args+=("$arg");;
    esac
done
exec /opt/devkitpro/tools/bin/elf2nro "${args[@]}"
