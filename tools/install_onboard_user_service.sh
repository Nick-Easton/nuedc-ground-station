#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
user_unit_dir="${HOME}/.config/systemd/user"

mkdir -p "${user_unit_dir}"
install -m 0644 \
  "${repo_root}/systemd/nuedc-ground-stack.service" \
  "${user_unit_dir}/nuedc-ground-stack.service"

systemctl --user daemon-reload
systemctl --user enable --now nuedc-ground-stack.service
systemctl --user --no-pager --full status nuedc-ground-stack.service
