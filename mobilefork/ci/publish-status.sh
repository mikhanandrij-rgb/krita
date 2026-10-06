#!/bin/bash
# SPDX-FileCopyrightText: 2026 Krita Mobile (unofficial fork) contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Krita Mobile (unofficial fork): pushes a CI job's results to the ci-status
# branch, so they can be read with plain git (the Actions API isn't reachable
# from the development sandbox).
#
# Usage: publish-status.sh <job-name> <job-status> <summary-file> [<dir-to-copy> <target-subdir>]...
# Requires GITHUB_TOKEN, GITHUB_REPOSITORY, GITHUB_RUN_NUMBER.
set -u

job="$1"; status="$2"; summary="$3"; shift 3

st="$(mktemp -d)"
remote="https://x-access-token:${GITHUB_TOKEN}@github.com/${GITHUB_REPOSITORY}.git"

for attempt in 1 2 3 4 5; do
    rm -rf "$st"; mkdir -p "$st"
    git -C "$st" init -q
    git -C "$st" remote add origin "$remote"
    if git -C "$st" fetch -q --depth=1 origin ci-status 2>/dev/null; then
        git -C "$st" checkout -q FETCH_HEAD
    else
        git -C "$st" checkout -q --orphan ci-status
    fi
    run="$st/runs/${GITHUB_RUN_NUMBER}/${job}"
    mkdir -p "$run"
    cp "$summary" "$run/summary.txt"
    { echo "== ${job} (run ${GITHUB_RUN_NUMBER}): ${status}"; cat "$summary"; echo; } > "$st/LATEST-${job}.txt"

    args=("$@")
    i=0
    while [ $i -lt ${#args[@]} ]; do
        src="${args[$i]}"; dst="${args[$((i+1))]}"
        i=$((i+2))
        [ -e "$src" ] || continue
        if [ "${dst#latest/}" != "$dst" ]; then
            # "latest/..." targets are replaced on every run instead of piling up.
            rm -rf "$st/$dst"
            mkdir -p "$st/$dst"
            cp -r "$src"/. "$st/$dst/"
        else
            mkdir -p "$run/$dst"
            cp -r "$src"/. "$run/$dst/"
        fi
    done

    # Keep the branch small: the last 15 runs of this job. (Run numbers are
    # per workflow, so other jobs' runs are left alone.)
    if [ -d "$st/runs" ]; then
        for d in "$st"/runs/*/"$job"; do [ -d "$d" ] && basename "$(dirname "$d")"; done | sort -n | head -n -15 |
            while read -r old; do
                rm -rf "$st/runs/$old/$job"
                rmdir "$st/runs/$old" 2>/dev/null || true
            done
    fi

    git -C "$st" add -A
    git -C "$st" -c user.name="ci-bot" -c user.email="ci-bot@users.noreply.github.com" \
        commit -q -m "CI run ${GITHUB_RUN_NUMBER} ${job}: ${status}" || true
    if git -C "$st" push -q origin HEAD:ci-status; then
        echo "Published ${job} status to ci-status."
        exit 0
    fi
    echo "Push failed (attempt ${attempt}), retrying..."
    sleep $((attempt * 7))
done
echo "::warning::Could not publish status to ci-status"
exit 0
