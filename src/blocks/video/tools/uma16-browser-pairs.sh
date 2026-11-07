#!/usr/bin/env bash
# Browsers capture at most two channels per microphone (Chrome): exposes the UMA16v2 corner microphones as two stereo
# PipeWire sources for the browser acoustic camera. Runtime only (gone after re-login); native applications keep using the
# 16-channel source. Usage: uma16-browser-pairs.sh setup|revert|status
set -euo pipefail

pairs=("uma16_pair_a UMA16_PAIR_A aux1,aux14" "uma16_pair_b UMA16_PAIR_B aux7,aux8") # rows: bottom, top

master() { pactl list sources short | awk '$2 ~ /^alsa_input\.usb-miniDSP_UMA16v2.*multichannel-input/ { print $2; exit }'; }

# the modules this script loaded, recognised by their source names
modules() { pactl list modules short | awk '$2 == "module-remap-source" && /source_name=uma16_pair_[ab]( |$)/ { print $1 }'; }

setup() {
  if [[ -n "$(modules)" ]]; then
    echo "already set up (modules $(modules | tr '\n' ' ')); run '$0 revert' first"
    exit 0
  fi
  local source
  source="$(master)"
  [[ -n "$source" ]] || { echo "no UMA16v2 multichannel input found" >&2; exit 1; }
  for pair in "${pairs[@]}"; do
    read -r name label channels <<<"$pair"
    pactl load-module module-remap-source source_name="$name" source_properties="device.description=$label" master="$source" \
      master_channel_map="$channels" channel_map=front-left,front-right remix=no >/dev/null || { revert; exit 1; }
    echo "$label: $channels of $source"
  done
}

revert() {
  local found
  found="$(modules)"
  [[ -n "$found" ]] || { echo "nothing to revert"; return 0; }
  while read -r module; do
    pactl unload-module "$module" && echo "unloaded module $module"
  done <<<"$found"
}

status() { pactl list sources short | grep -E 'uma16_pair_[ab]' || echo "no pair sources"; }

case "${1:-}" in
  setup) setup ;;
  revert) revert ;;
  status) status ;;
  *)
    echo "usage: $0 setup|revert|status" >&2
    exit 2
    ;;
esac
