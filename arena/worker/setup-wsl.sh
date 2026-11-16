#!/usr/bin/env bash
# One-time setup of the FairAI training worker inside WSL2 (Ubuntu 24.04).
#
#   curl -sSLO https://raw.githubusercontent.com/MassGnolllls/vcmi/fairai/arena/worker/setup-wsl.sh
#   bash setup-wsl.sh
#
# Installs build tools, fetches and builds VCMI+FairAI (headless), copies the Heroes III data
# files, stores a GitHub token for uploading results and creates the command `fairai-worker`.
set -euo pipefail

REPO_URL="https://github.com/MassGnolllls/vcmi.git"
BRANCH="fairai"
ROOT="$HOME/fairai"
SRC="$ROOT/vcmi"
BUILD="$ROOT/build"
DATA="$HOME/.local/share/vcmi/Data"

ask() { local reply; read -r -p "$1" reply < /dev/tty; echo "$reply"; }

echo
echo "== 1/5 Programme installieren (Ubuntu fragt nach deinem Linux-Passwort)"
sudo apt-get update
# Ubuntu 22.04 ships g++ 11, which is too old for parts of VCMI's C++20: use g++ 12 there
if [ "$(. /etc/os-release && echo "$VERSION_ID")" = "22.04" ]; then
    sudo apt-get install -y gcc-12 g++-12
    export CC=gcc-12 CXX=g++-12
fi
sudo apt-get install -y git cmake ninja-build g++ ccache python3 \
    libsdl2-dev libsdl2-image-dev libsdl2-ttf-dev libsdl2-mixer-dev zlib1g-dev \
    libboost-dev libboost-filesystem-dev libboost-system-dev libboost-thread-dev \
    libboost-program-options-dev libboost-locale-dev libboost-iostreams-dev \
    libtbb-dev libluajit-5.1-dev liblzma-dev libsqlite3-dev libminizip-dev libsquish-dev libfmt-dev

echo
echo "== 2/5 Quellcode holen"
mkdir -p "$ROOT"
if [ ! -d "$SRC/.git" ]; then
    git clone --branch "$BRANCH" --depth 50 "$REPO_URL" "$SRC"
fi
git -C "$SRC" submodule update --init --depth 1 AI/FuzzyLite

echo
echo "== 3/5 Bauen (beim ersten Mal 30-60 Minuten)"
cmake -S "$SRC" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DENABLE_LAUNCHER=OFF -DENABLE_EDITOR=OFF -DENABLE_LOBBY=OFF -DENABLE_TRANSLATIONS=OFF \
    -DENABLE_VIDEO=OFF -DENABLE_DISCORD=OFF -DENABLE_MMAI=OFF -DENABLE_MONOLITHIC_INSTALL=ON -DENABLE_CCACHE=ON
cmake --build "$BUILD" -j"$(nproc)"

echo
echo "== 4/5 Spieldaten von Heroes III (H3bitmap.lod, H3sprite.lod, H3ab_bmp.lod, H3ab_spr.lod)"
mkdir -p "$DATA"
found=""
for dir in /mnt/c/FairAI-Daten /mnt/c/FairAI-Daten/Data /mnt/c/Users/*/Documents/"My Games"/vcmi/Data \
           "/mnt/c/GOG Games/"*/Data "/mnt/c/Program Files (x86)/"*/Data; do
    if [ -f "$dir/H3bitmap.lod" ] || [ -f "$dir/h3bitmap.lod" ]; then found="$dir"; break; fi
done
if [ -z "$found" ]; then
    echo "Ich finde die Dateien nicht. Lege sie z. B. nach C:\\FairAI-Daten und gib den Ordner hier an,"
    found="$(ask 'Ordner (Linux-Schreibweise, z. B. /mnt/c/FairAI-Daten): ')"
fi
cp -v "$found"/*.lod "$found"/*.LOD "$DATA"/ 2>/dev/null || true
ls "$DATA" | grep -qi "h3bitmap.lod" || { echo "FEHLER: keine .lod-Dateien in $DATA"; exit 1; }

echo
echo "== 5/5 Zugang zum Hochladen der Ergebnisse"
echo "Lege auf github.com einen 'Fine-grained token' an: Settings > Developer settings >"
echo "Personal access tokens > Fine-grained tokens > Generate new token."
echo "Repository access: Only select repositories > MassGnolllls/vcmi; Permissions: Contents = Read and write."
token="$(ask 'Token hier einfügen (Rechtsklick): ')"
git config --global credential.helper store
printf 'https://MassGnolllls:%s@github.com\n' "$token" > "$HOME/.git-credentials"
chmod 600 "$HOME/.git-credentials"
git config --global user.name "FairAI worker"
git config --global user.email "fairai-worker@users.noreply.github.com"
git -C "$SRC" ls-remote --heads origin "$BRANCH" > /dev/null && echo "GitHub-Zugang ok."

mkdir -p "$HOME/.local/bin"
cat > "$HOME/.local/bin/fairai-worker" <<EOF
#!/usr/bin/env bash
exec python3 "$SRC/arena/worker/worker.py" "\$@"
EOF
chmod +x "$HOME/.local/bin/fairai-worker"

echo
echo "Fertig. Starte den Trainings-Dienst mit:   ~/.local/bin/fairai-worker"
echo "Das Fenster offen lassen; beenden mit Strg+C."
