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

## Wachen-Prüfstand und Kämpfe aus echten Partien

```bash
# Wachen-Prüfstand: ein Held mit plausibler Armee gegen Wachen jeder Kreatur (30, 50, 80 % der eigenen Stärke)
python3 arena/guard_bench.py gen  --client build/bin/vcmiclient
python3 arena/guard_bench.py play --client build/bin/vcmiclient --ai FairAI --filter __0. --out guard-a \
    --ai-params '{"battlePlan": 3, "guardModel": 1, "ownCasters": 1, "wholeCreatures": 1}'

# Kämpfe aus mitgeschriebenen Partien nachstellen (Armee, wie sie stand; Wachen nach Art und Zahl):
# die Sätze liegen im Fork und werden einmal zu Karten gemacht
python3 arena/game_fights.py gen --set arena/gamefights/sample300.json --client build/bin/vcmiclient
python3 arena/game_fights.py gen --set arena/gamefights/dearest69.json --client build/bin/vcmiclient
python3 arena/guard_bench.py play --client build/bin/vcmiclient --ai FairAI --filter __s0 --out games-a --ai-params '{...}'

# neue Sätze aus eigenen Logs (Partien mit ARENA_AI_LOG=debug)
python3 arena/game_fights.py collect --logs 'probes/*/VCMI_Client_log.txt' --out arena/gamefights/neu.json \
    --sample 300 --suffix __s1 --client build/bin/vcmiclient

# Kämpfe mit dem Helden, wie er in den Kampf ging (Stufe, Werte, Zauber, Zauberpunkte, Kriegsmaschinen):
# wie in der Partie, mit vollem Vorrat an Zauberpunkten und ganz ohne
python3 arena/game_fights.py gen --set arena/gamefights/heroes239.json --client build/bin/vcmiclient
python3 arena/game_fights.py gen --set arena/gamefights/heroes239.json --mana full --suffix __f0 --client build/bin/vcmiclient
python3 arena/game_fights.py gen --set arena/gamefights/heroes239.json --mana none --suffix __n0 --client build/bin/vcmiclient

# dieselben Kämpfe, die Wachen von einer anderen Kampf-KI gespielt (Voreinstellung des Spiels: BattleAI)
ARENA_NEUTRAL_AI=StupidAI python3 arena/guard_bench.py play --client build/bin/vcmiclient --ai FairAI --filter __s0 --out games-stupid
```

- `sample300` (Endung `__s0`): 300 zufällige von 2.685 Wachenkämpfen aus 91 Partien – so sehen die Kämpfe einer Partie aus
  (die Hälfte kostet nichts; Wachen, die schießen, sind ein Viertel der Kämpfe und fast die Hälfte der Verluste).
- `dearest69` (Endung `__g0`): die 69 teuersten (ab 20 % Verlust oder 10 Punkte über der Prognose der Kampf-KI).
- Stand 19.11.2026 auf `sample300`: ohne Kampf-Paket 7,1 % der Armee verloren, mit 4,5 % (160 statt 75 Kämpfe ohne Verlust).
  Mit Wachen, die StupidAI spielt: 5,7 % und 3,6 %; mit MMAI: 7,3 % und 5,2 % (20.11.).
- `heroes239` (Endung `__h0`): alle 239 Wachenkämpfe aus 13 Partien vom 20.11., jeder mit dem Helden, wie er war. Die Helden
  haben in 215 der Kämpfe Zauber, aber im Mittel nur 9 von 26 Zauberpunkten. Mit dem Kampf-Paket: ohne Zauberpunkte 5,9 %
  Verlust, mit denen der Partie 5,4 % (3,8 Punkte je Kampf ausgegeben), mit vollem Vorrat 4,9 % (11,6 Punkte).
  Das Ergebnis eines Kampfes nennt die Zauberpunkte, die der Held übrig hat (`heroMana`).
- Die Endung eines Satzes darf in keinem anderen Kartennamen vorkommen (`--filter` sucht im Namen; `__r0` träfe auch `__r030__0`).

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

## Spiegelkarten (beide Spieler mit derselben Welt)

Wie bei HotAs Spiegelvorlagen ist der Untergrund eine exakte Kopie der Oberfläche, je Ebene ein Spieler.
`arena/mirror_maps.py` macht aus einer Karte für zwei Spieler aus VCMIs Generator (eine Ebene) die Welt des
ersten Spielers: die Stadt des zweiten fällt weg, ein Höhlentor kommt dorthin, wo sich die Wege beider Städte
treffen, dann wird alles in den Untergrund kopiert; die Kopie der ersten Stadt gehört dem zweiten Spieler.
Gral, Obelisken, Kartografen und Gefängnisse fallen weg; Monolithe der Kopie bekommen eigene Farben.
Das Wort `arena-mirror` in der Kartenbeschreibung sorgt dafür, dass das Spiel beim Start für ein Objekt und
seine Kopie dasselbe würfelt (Platzhalter, Banken, Schreine, Magiergilden, Startboni, Teilung der Monster);
jeder Start prüft das selbst („Mirror check: N pairs, 0 differ“ im Protokoll).

```bash
python3 arena/mirror_maps.py make --client build/bin/vcmiclient --out arena/maps/mirror-m --seed 52000   # 12 Stadttypen
python3 arena/mirror_maps.py play --src maps/mirror-m --pool mirror-m -- --client build/bin/vcmiclient \
    --ai FairAI+FairAI --opponent Nullkiller2+MMAI --mods hota --max-days 300 --jobs 6 --out out
```

Die zwölf M-Karten vom 24.11. (je eine Stadt für beide Spieler) liegen in `arena/maps/mirror-m`.

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
5. Was ein Gebäude gibt und wer es bewacht: FairAI kennt nur die öffentliche Tabelle der Gebäudeart – was ein solches Gebäude enthalten kann und wie oft (das Regelwissen jedes erfahrenen Spielers) –, nie den ausgewürfelten Inhalt des einzelnen Gebäudes auf der Karte. Die Wachen einer Kreaturenbank kennt sie erst, wenn das Spiel sie anzeigt: in der Frage vor dem Kampf und danach per Rechtsklick.
6. Startressourcen: In KI-gegen-KI-Partien bekommen beide Seiten dieselben. Für Partien Mensch gegen FairAI ist der Schwierigkeitsgrad „Turm“ (rook) der faire: In diesem Fork starten Mensch und KI dort mit exakt denselben Ressourcen (im Original bekommt die KI 5000 Gold weniger). Auf „König“ startet der Mensch dagegen ohne Ressourcen.

## Battles played from outside the game (both sides)

`arena/puppet.py` plays a battle with both sides steered by strategies written in Python
(`arena/puppet_plays.py`), with the game itself as the referee: with `FAI_PUPPET_PORT` set,
FairBattleAI describes the battle before every decision of such a side as one line of JSON to a
listener on this machine and plays the action it answers (`AI/FairAI/Combat/Puppet.h`). A side can
also be left to a combat AI (`fairai`, `battleai`, `stupidai`). Nothing of this runs in a real game.

```
python3 arena/puppet.py play --client build/bin/vcmiclient --scen arena/puppet_scen/bild2.json \
    --left kite --right battleai --seeds 50 [--nodice] --out out
python3 arena/puppet.py list out/trace_tb_bild2_kite_battleai_x1.json       # every action of one battle
python3 arena/puppet.py show out/trace_tb_bild2_kite_battleai_x1.json --step 0   # the field before an action
```

Three ways to play a battle: as the game plays it; `--nodice`, without the dice of morale and luck
(`ARENA_NO_MORALE_LUCK`); with a fixed seed (`--first-seed N --seeds 1` - the same seed and the same
actions give the same battle). Battles in which nobody gets anywhere are ended after 250 rounds.

A scenario (`arena/puppet_scen/*.json`) is a scenario of `--arena-genbattle` with the place of the
battle: `mapSize`, `tile` (the obstacles of a battlefield follow from the tile alone), `rows` (bands
of another terrain: water north of a beach) and the monster's own split (`stacks`, `upgraded`). A
battle of a real map is rebuilt from `vcmiclient --headless --arena-dump-map MAPS/DIR/NAME --arena-out
FILE` (objects, armies, the monsters' split settings, terrain). A hero named in a scenario has exactly
the skills the scenario lists - none, if it lists none.

`arena/pursuit.py` answers exactly whether a fast flyer nobody retaliates against can wear down one
walking stack without ever being struck: every position of the field is gone through, no sampling
(`--obst` hexes of the obstacles, `--kiter` / `--chaser` starting hexes, `--kspeed` / `--cspeed`).
The five battles of the user's test map of 23.11.2026 are `arena/puppet_scen/bild1.json` to
`bild5.json`; the strategies found for them are in `arena/puppet_plays.py`.
