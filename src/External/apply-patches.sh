#!/usr/bin/env bash
# Applies src/External/patches/<dep>/*.patch onto the matching submodule working tree.
#
# The glslang/spirv-cross submodules point at pristine KhronosGroup upstream tags;
# this repository carries a local patch series (Wallpaper Engine shader-compatibility
# patches, originally from the Almamu/*-WallpaperEngine forks) that must be applied
# after every `git submodule update --init`.
#
# Idempotent and self-healing: a stamp records the submodule HEAD, the patch-series
# checksum and the checksum of the applied diff. The series is reapplied only when
# one of those changed (for example after a patch edit or a submodule update), in
# which case the submodule's tracked files are restored before applying the whole
# series again. The submodules are managed dependencies: local changes inside them
# are discarded.
set -euo pipefail
cd "$(dirname "$0")/../.."

stampdir=".cache/wallpaper-engine-patches"

for dep in glslang spirv-cross; do
    dir="src/External/$dep"
    patchdir="src/External/patches/$dep"
    stampfile="$stampdir/$dep.stamp"

    if [ ! -d "$dir" ] || ! git -C "$dir" rev-parse --git-dir > /dev/null 2>&1; then
	echo "ERROR: $dir is missing or not a git checkout, run: git submodule update --init $dir" >&2
	exit 1
    fi

    if ! compgen -G "$patchdir/*.patch" > /dev/null; then
	echo "ERROR: no patches found in $patchdir" >&2
	exit 1
    fi

    head=$(git -C "$dir" rev-parse HEAD)
    series=$(cat "$patchdir"/*.patch | sha256sum | cut -d' ' -f1)
    applied=$(git -C "$dir" diff HEAD --binary --no-ext-diff | sha256sum | cut -d' ' -f1)

    if [ -f "$stampfile" ]; then
	stamp_head=
	stamp_series=
	stamp_applied=
	read -r stamp_head stamp_series stamp_applied < "$stampfile" || true
	if [ "$stamp_head" = "$head" ] \
	   && [ "$stamp_series" = "$series" ] \
	   && [ "$stamp_applied" = "$applied" ]; then
	    echo "== $dep: patch series already applied, skipping"
	    continue
	fi
    fi

    echo "== $dep: applying patch series"
    rm -f "$stampfile"
    git -C "$dir" reset --hard -q "$head"
    git -C "$dir" clean -fdq

    for patch in "$patchdir"/*.patch; do
	echo "   applying $(basename "$patch")"
	git -C "$dir" apply "$PWD/$patch"
    done

    # guards against a broken glob silently applying nothing
    if [ -z "$(git -C "$dir" status --porcelain)" ]; then
	echo "ERROR: patch series applied no changes in $dir" >&2
	exit 1
    fi

    # the current series only modifies tracked files; if a future patch adds files,
    # switch to `git apply --index` and hash `git diff HEAD` after staging them
    applied=$(git -C "$dir" diff HEAD --binary --no-ext-diff | sha256sum | cut -d' ' -f1)
    mkdir -p "$stampdir"
    printf '%s %s %s\n' "$head" "$series" "$applied" > "$stampfile.tmp"
    mv "$stampfile.tmp" "$stampfile"
done

echo "All patches applied."
