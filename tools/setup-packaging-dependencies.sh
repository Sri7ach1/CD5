#!/usr/bin/env bash
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
kind=${1:-}

case "$kind" in
    ffpfsc)
        for command in git python3; do
            command -v "$command" >/dev/null || {
                echo "missing required command: $command" >&2
                exit 2
            }
        done

        checkout="$root/.deps/MkPFS"
        revision="6cb8313dfe0c988ac52617794553f343243d3a56"

        if [[ ! -d $checkout/.git ]]; then
            mkdir -p "$checkout"
            git -C "$checkout" init --quiet
            git -C "$checkout" remote add origin https://github.com/PSBrew/MkPFS.git
            git -C "$checkout" fetch --quiet --depth 1 origin "$revision"
            git -C "$checkout" checkout --quiet --detach FETCH_HEAD
        fi

        actual=$(git -C "$checkout" rev-parse HEAD)

        [[ $actual == "$revision" ]] || {
            echo "MkPFS cache is at $actual; expected $revision" >&2
            exit 2
        }

        git -C "$checkout" diff --quiet || {
            echo "MkPFS cache has local changes" >&2
            exit 2
        }

        venv="$checkout/.venv-linux"
        python="$venv/bin/python"
        stamp="$venv/.cd5-revision"

        if [[ ! -x $python ]]; then
            rm -rf -- "$venv"
            python3 -m venv "$venv" || {
                echo "Python venv support is required (install python3-venv)" >&2
                exit 2
            }
        fi

        if ! "$python" -m pip --version >/dev/null 2>&1; then
            python3 -m pip --python "$python" install \
                --disable-pip-version-check --quiet pip >&2
        fi

        if [[ ! -f $stamp || $(<"$stamp") != "$revision" ]]; then
            "$python" -m pip install \
                --disable-pip-version-check \
                --quiet \
                "$checkout" >&2

            printf '%s\n' "$revision" > "$stamp"
        fi

        runner="$checkout/.mkpfs-run-linux"

        printf '#!/bin/sh\nexec "%s" -m mkpfs "$@"\n' \
            "$python" > "$runner"

        chmod +x "$runner"

        "$runner" --help >/dev/null

        printf '%s\n' "$runner"
        ;;

    *)
        echo "usage: tools/setup-packaging-dependencies.sh ffpfsc" >&2
        exit 2
        ;;
esac
