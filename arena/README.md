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

## Fairness-Regeln für FairAI

1. Keine Cheat-Codes, keine Chat-Befehle. In Turnieren sind Cheats serverseitig verboten, jeder Versuch wird als `cheated` im Ergebnis gemeldet.
2. Kein Zugriff auf den vollen Spielzustand oder die rohe Karte.
3. Fremde Armeen, Helden und Städte nur über `Perception` – also genau das, was die Oberfläche einem Menschen zeigen würde (Mengenangaben wie „Rudel = 10–19“ statt exakter Zahlen).
4. Wieder verdeckte Gebiete (Schleier der Dunkelheit) behalten den letzten Stand mit Zeitstempel, werden aber als veraltet markiert.
5. Startressourcen: In KI-gegen-KI-Partien bekommen beide Seiten dieselben. Für Partien Mensch gegen FairAI ist der Schwierigkeitsgrad „Turm“ (rook) der faire: In diesem Fork starten Mensch und KI dort mit exakt denselben Ressourcen (im Original bekommt die KI 5000 Gold weniger). Auf „König“ startet der Mensch dagegen ohne Ressourcen.
