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

## Was der Worker außer dem Fork lädt

Nur, was der Besitzer des Rechners ausdrücklich erlaubt hat, und nur in dem Stand, der im Fork hinterlegt ist. Der gespeicherte Token wird dafür nie verwendet; alles landet im eigenen Ordner des Workers bzw. im Mod-Ordner von VCMI, ohne Administratorrechte.

| Was | Woher | Festgelegt durch | Datei |
|---|---|---|---|
| Horn of the Abyss (Mod) | `vcmi-mods/horn-of-the-abyss` | Git-Commit | `mods.json` |
| MMAI: Modelle und Einstellungen (Mod, nur Daten) | Veröffentlichung von `vcmi-mods/mmai`, das Archiv, das auch der VCMI-Launcher installiert | SHA-256 des Archivs | `mods.json` |
| ONNX Runtime 1.18.1 (Bibliothek, mit der MMAI seine Modelle rechnet) | offizielle Veröffentlichung von `microsoft/onnxruntime`, wie in der VCMI-Bauanleitung genannt | SHA-256 jeder verwendeten Datei; eine abweichende Datei wird nicht auf die Platte geschrieben | `tools.json` |

Ein Job fordert Mods mit `"mods": ["hota"]` bzw. `"mods": ["mmai"]` an. Für MMAI nimmt der Worker die Bibliothek in den Build auf (nur MMAI selbst wird dabei übersetzt). Lässt sich etwas nicht im hinterlegten Stand beschaffen, schlägt nur dieser Job fehl; der nächste läuft.
