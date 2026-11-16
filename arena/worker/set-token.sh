#!/usr/bin/env bash
# Replace the GitHub token the worker uses to upload results.
set -euo pipefail
read -rs -p "Neuen GitHub-Token einfügen (Rechtsklick - er bleibt unsichtbar), dann Enter: " token < /dev/tty
echo
token="$(printf '%s' "$token" | tr -d '[:space:]')"
[ -n "$token" ] || { echo "Kein Token eingegeben - nichts geändert."; exit 1; }
git config --global credential.helper store
printf 'https://MassGnolllls:%s@github.com\n' "$token" > "$HOME/.git-credentials"
chmod 600 "$HOME/.git-credentials"
# check write access without changing anything on GitHub (dry run to a branch name that does not exist)
if GIT_TERMINAL_PROMPT=0 git -C "$HOME/fairai/vcmi" push --dry-run -q origin HEAD:refs/heads/fairai-token-check 2>/dev/null; then
    echo "Token funktioniert."
else
    echo "WARNUNG: GitHub hat den Token nicht angenommen. Prüfe: nur Repository MassGnolllls/vcmi, Contents = Read and write."
fi
echo "Jetzt den Trainings-Dienst starten:   ~/.local/bin/fairai-worker"
