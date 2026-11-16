# Trainings-Rechner (FairAI-Worker)

Ein zweiter PC spielt Testserien und Tuning-Läufe, die Ergebnisse landen im Branch `fairai-results` des Forks.

1. WSL2 installieren: PowerShell als Administrator, `wsl --install -d Ubuntu-24.04`, neu starten, Linux-Benutzer anlegen.
2. Die vier Heroes-III-Dateien `H3bitmap.lod`, `H3sprite.lod`, `H3ab_bmp.lod`, `H3ab_spr.lod` (Ordner `Dokumente\My Games\vcmi\Data` auf dem Hauptrechner) nach `C:\FairAI-Daten` kopieren.
3. Auf github.com einen Fine-grained Token nur für `MassGnolllls/vcmi` mit „Contents: Read and write“ anlegen.
4. Im Ubuntu-Fenster:
   ```bash
   curl -sSLO https://raw.githubusercontent.com/MassGnolllls/vcmi/fairai/arena/worker/setup-wsl.sh
   bash setup-wsl.sh
   ~/.local/bin/fairai-worker
   ```

Aufträge sind JSON-Dateien in `arena/jobs/` (siehe `worker.py`). Der Worker führt nur Code aus dem Branch `fairai` des Forks aus und lädt nur kleine Ergebnisdateien hoch (keine Logs).
