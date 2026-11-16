#!/usr/bin/env bash
# Replace the GitHub token the worker uses to upload results.
# The token is checked against GitHub first; ~/.git-credentials is only changed when it works.
set -uo pipefail
echo "Kopiere jetzt auf github.com den Token (Fine-grained tokens > dein Token > 'Regenerate token',"
echo "falls du ihn nicht mehr hast) und füge ihn hier mit Rechtsklick ein. Er bleibt unsichtbar."
read -rs -p "Token einfügen, dann Enter: " token < /dev/tty
echo
token="$(printf '%s' "$token" | tr -d '[:space:]')"
if [ -z "$token" ]; then
    echo "Kein Token eingegeben - nichts geändert."
    exit 1
fi
case "$token" in
    github_pat_*|ghp_*) echo "Eingegeben: ${token:0:11}... (${#token} Zeichen)" ;;
    *)
        echo "Das war kein Token (GitHub-Tokens beginnen mit github_pat_). Vermutlich war noch etwas anderes"
        echo "in der Zwischenablage. Bitte den Token auf GitHub kopieren und dieses Skript nochmal starten."
        exit 1 ;;
esac

# dry run of a push to a branch name that does not exist: needs write access, changes nothing
errors="$(mktemp)"
export FAIRAI_TOKEN="$token"
if GIT_TERMINAL_PROMPT=0 git -C "$HOME/fairai/vcmi" -c credential.helper= \
        -c 'credential.helper=!f() { echo username=x-access-token; echo "password=$FAIRAI_TOKEN"; }; f' \
        push --dry-run -q origin HEAD:refs/heads/fairai-token-check 2>"$errors"; then
    git config --global credential.helper store
    printf 'https://MassGnolllls:%s@github.com\n' "$token" > "$HOME/.git-credentials"
    chmod 600 "$HOME/.git-credentials"
    echo "Token funktioniert und ist gespeichert."
    echo "Jetzt den Trainings-Dienst starten:   ~/.local/bin/fairai-worker"
else
    echo "GitHub hat den Token nicht angenommen - nichts geändert. GitHub sagt:"
    grep -v '^\s*$' "$errors" | tail -3 | sed 's/^/    /'
    echo "Prüfe: Repository MassGnolllls/vcmi, Contents = Read and write. Dann dieses Skript nochmal starten."
fi
rm -f "$errors"
