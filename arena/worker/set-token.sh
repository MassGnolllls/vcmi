#!/usr/bin/env bash
# Replace the GitHub token the worker uses to upload results.
set -euo pipefail
read -r -p "Neuen GitHub-Token einfügen (Rechtsklick), dann Enter: " token < /dev/tty
printf 'https://MassGnolllls:%s@github.com\n' "$token" > "$HOME/.git-credentials"
chmod 600 "$HOME/.git-credentials"
echo "Gespeichert. Den Trainings-Dienst neu starten: Strg+C, dann ~/.local/bin/fairai-worker"
