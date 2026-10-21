#!/bin/sh
set -eu

localedir="$MESON_INSTALL_DESTDIR_PREFIX/$1"
target="$MESON_INSTALL_DESTDIR_PREFIX/$2"
domain="$3"

for mo in "$localedir"/*/LC_MESSAGES/"$domain".mo; do
    [ -e "$mo" ] || continue
    lang=$(basename "$(dirname "$(dirname "$mo")")")
    mkdir -p "$target/$lang/LC_MESSAGES"
    cp "$mo" "$target/$lang/LC_MESSAGES/$domain.mo"
done
