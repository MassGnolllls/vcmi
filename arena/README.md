# Arena – KI-Turniere ohne Grafik

Werkzeuge, um VCMI-KIs objektiv gegeneinander zu messen. Grundlage für die Entwicklung von FairAI.

## Bausteine

| Teil | Wo | Zweck |
|---|---|---|
| Arena-Optionen im Client | `client/ArenaHarness.*`, `clientapp/EntryPoint.cpp` | KI pro Spieler, fester Seed, Tageslimit, Ergebnisdatei (JSON), Kartengenerator |
| Turnier-Skript | `arena/run_tournament.py` | Karten erzeugen, Partien mit Seitenwechsel spielen, Auswertung |
| Fairness-Prüfung | `arena/check_fairness.py` | Prüft den FairAI-Code auf verbotene Zugriffe (Cheats, voller Spielzustand) |
| Kampf-Arena | `--arena-genbattle`, `arena/battle_scenarios.py`, `run_tournament.py battles` | Einzelne Kämpfe (VCMIs „Battle Only“-Modus) zwischen zwei Kampf-KIs |
| Variantenvergleich | `arena/compare_runs.py` | Gepaarter Vergleich zweier Läufe auf denselben Szenarien |

## Neue Kommandozeilen-Optionen (vcmiclient)

```
--headless --testmap MAPS/ARENA/<karte>      Partie ohne Grafik, nur KIs
--ai <Name> --ai <Name>                      KI für Rot, Blau, ... (z. B. FairAI, Nullkiller2)
--arena-result <datei.json>                  Ergebnis schreiben und beenden, sobald entschieden
--arena-max-days <n>                         nach n Tagen als "timeout" beenden
--arena-seed <n>                             reproduzierbarer Zufall
--arena-difficulty <0-4>                     Schwierigkeitsgrad (beide KIs bekommen dieselben Startressourcen)
--arena-allow-cheats                         Cheat-Codes erlauben (Standard: verboten)
--arena-genmap <datei.vmap> [--arena-template 2SM4d --arena-size s --arena-levels 1 --arena-seed n
               --arena-water none --arena-monsters normal]   Zufallskarte erzeugen
```

## Ablauf

```bash
# Karten einmalig erzeugen (landen in <VCMI-Datenordner>/Maps/arena/)
python3 arena/run_tournament.py genmaps --client build/bin/vcmiclient --count 10 --template 2SM4d --size s

# Turnier: jede Karte zweimal, mit vertauschten Seiten
python3 arena/run_tournament.py play --client build/bin/vcmiclient \
    --ai FairAI --opponent Nullkiller2 --max-days 120 --jobs 2 --out arena-results/fair-vs-nk2

# Fairness-Prüfung des Codes
python3 arena/check_fairness.py
```

## Kampf-Arena

```bash
# Spieldaten-Bibliothek exportieren und 200 Kampfszenarien erzeugen (Spiegel- und Kreuzkämpfe,
# frühe/mittlere/späte Armeen, mit und ohne Zauber)
python3 arena/run_tournament.py genbattles --client build/bin/vcmiclient --count 200 --prefix dev

# Kampf-KI gegen Kampf-KI, jedes Szenario mit vertauschten Seiten
python3 arena/run_tournament.py battles --client build/bin/vcmiclient --ai FairAI --opponent MMAI \
    --map-filter dev_ --jobs 2 --out battle-results/fair-vs-mmai

# zwei Varianten auf denselben Szenarien vergleichen (z. B. mit FAI_ROUNDS=1 gegen Standard)
python3 arena/compare_runs.py battle-results/variante-a battle-results/variante-b
```

Score eines Kampfes = eigener verbleibender Armee-Anteil minus gegnerischer verbleibender Anteil (−1 … +1).
MMAI braucht seine Mod im VCMI-Datenordner; für VCMI 1.7.x die Version aus dem offiziellen Mod-Repository (Modelle mit Endung `-stochastic.onnx`).

Ergebnis: `results.jsonl` (eine Zeile pro Partie) und `summary.md` mit Siegquote und 95-%-Konfidenzintervall.

## Auswertung ganzer Partien

```bash
python3 run_tournament.py play --client VCMICLIENT --ai FairAI --opponent Nullkiller2 \
    --map-filter 2SM4d_s_l1_none --max-maps 12 --max-days 28 --jobs 2 --keep-logs --out lauf1
python3 game_stats.py lauf1 --brief     # Armee, Städte, Helden, Gold, erkundeter Anteil je Seite
python3 battle_stats.py lauf1 --list 20 # FairAIs Kämpfe: wer greift wen an, wer gewinnt
```

- Labels `Abenteuer+Kampf` (z. B. `Nullkiller2+FairAI`) wählen beide KIs eines Platzes.
- `FAI_PARAMS='{"safetyRatio":1.2}'` überschreibt FairAI-Parameter (siehe `AI/FairAI/Adventure/Params.h`).
- `ARENA_AI_LOG=debug` schreibt die Entscheidungen der KIs ins Log (`--keep-logs`), `FAI_DUMP_MAP=1` zusätzlich FairAIs Kartenwissen an den Tagen 7, 14 und 28.
- Für lange Läufe eine feste Kopie des Builds verwenden (sonst wechseln laufende Turniere mitten drin die Bibliothek), z. B. `cp -al build/bin snap/v1` und `--client snap/v1/vcmiclient`.

## Automatisches Tuning (Phase 4)

`tune.py` sucht bessere FairAI-Parameter (siehe `AI/FairAI/Adventure/Params.h`) per (1+λ)-Evolutionsstrategie: Mutanten der besten Einstellung spielen auf denselben Karten (beide Startplätze) gegen einen festen Gegner; Punkte je Partie: Sieg 1, Niederlage 0, sonst Armee-Anteil am Tageslimit.

```bash
python3 tune.py --client snap/v1/vcmiclient --opponent Nullkiller2 --maps 12 --days 28 \
    --generations 30 --lam 4 --jobs 6 --out tune1
FAI_PARAMS="$(cat tune1/best.json)" python3 run_tournament.py play ...   # Ergebnis prüfen
```

Eine Bewertung (24 Partien à 28 Tage) dauert mit 2 Kernen etwa 5 Minuten, mit 6 Kernen etwa 2. Für ernsthaftes Tuning (Hunderte Bewertungen) ist der Zweitrechner gedacht. Per Seat lassen sich Parameter auch direkt setzen: `run_tournament.py play --ai-params '{...}' --opponent-params '{...}'` (Client-Option `--arena-fairai-params`).

## A/B-Tests (gepaart)

`ab_test.py` lässt mehrere Parametersätze mit demselben Build exakt dieselben Partien spielen (Karten, Seiten, Spiel-Seeds) und vergleicht jede Partie mit der ersten Konfiguration. Die Paarung entfernt den Großteil des Rauschens zwischen Karten. Neue Funktionen kommen deshalb hinter einen Parameter (alt/neu umschaltbar) und werden so gemessen.

```bash
python3 ab_test.py --client snap/v1/vcmiclient --out ab1 --jobs 6 --seeds 42,43 \
    --configs '{"base": {}, "x": {"safetyRatio": 1.2}}' --map-filter 2SM4d_s_l1_none --max-maps 36 --max-days 45
```

Ergebnis: `ab1/ab_summary.md` (Punkte je Konfiguration und mittlere Differenz zur Basis mit 95%-Intervall). Auf dem Zweitrechner sind das Jobs mit `"script": "ab_test.py"`; `job_report.py` zeigt die Tabellen an.

## Fairness-Regeln für FairAI

1. Keine Cheat-Codes, keine Chat-Befehle. In Turnieren sind Cheats serverseitig verboten, jeder Versuch wird als `cheated` im Ergebnis gemeldet.
2. Kein Zugriff auf den vollen Spielzustand oder die rohe Karte.
3. Fremde Armeen, Helden und Städte nur über `Perception` – also genau das, was die Oberfläche einem Menschen zeigen würde (Mengenangaben wie „Rudel = 10–19“ statt exakter Zahlen).
4. Wieder verdeckte Gebiete (Schleier der Dunkelheit) behalten den letzten Stand mit Zeitstempel, werden aber als veraltet markiert.
5. Startressourcen: In KI-gegen-KI-Partien bekommen beide Seiten dieselben. Für Partien Mensch gegen FairAI ist der Schwierigkeitsgrad „Turm“ (rook) der faire: In diesem Fork starten Mensch und KI dort mit exakt denselben Ressourcen (im Original bekommt die KI 5000 Gold weniger). Auf „König“ startet der Mensch dagegen ohne Ressourcen.
