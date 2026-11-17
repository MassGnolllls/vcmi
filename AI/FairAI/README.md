# FairAI

Eine KI für VCMI (Heroes of Might and Magic III), die ohne Schummeln spielt: kein Aufdecken der Karte, keine Extraboni, kein Wissen, das ein Mensch auf demselben Platz nicht hätte.

## Aufbau

```
FairAI.h/.cpp          Gateway: Ereignisse und Anfragen der Engine, Zug-Thread, Rückzugsentscheidung
Status.h/.cpp          Worauf der Zug-Thread warten muss (Dialoge, Kämpfe, Bewegung)
Perception/            Fairness-Grenze: alles Wissen über fremde Objekte läuft hier durch
Combat/                Kampf-KI (Phase 1): schneller Kampfsimulator + Vorausschau-Suche
Adventure/AdventureAI  Abenteuerkarte (Phase 2/3): Ziele, Wirtschaft, Städte, Bedrohungen
Adventure/Params.h     alle Gewichte als Parameter (Grundlage für Phase 4)
```

## Stand

**Phase 0 (Infrastruktur):** Das Gateway beantwortet alle Engine-Anfragen korrekt, hält seine Wahrnehmung aktuell (inkl. wieder verdeckter Felder) und beendet den Zug.

**Phase 1 (Kampf):** FairAI kämpft mit eigenem Kampfmodul (`Combat/`):
- `CombatSim`: eigener, schneller Kampfsimulator. Schaden pro Kreatur für jedes Einheitenpaar kommt aus der Schadensberechnung der Engine, die Abläufe (Zugreihenfolge, Bewegung, Vergeltung, Doppelangriffe, Schützen, Munition, Blockieren) simuliert FairAI selbst.
- `FairBattleAI`: Für jede Einheit werden alle legalen Angriffe, Schüsse, einige Bewegungsziele, Warten und Verteidigen bis zum Rundenende durchgespielt; gewählt wird die Aktion mit der besten Materialbilanz.
- Heldenzauber, Katapult, Erste-Hilfe-Zelt, Berserker und zaubernde Kreaturen übernehmen vorerst Bausteine von VCMIs BattleAI. Aussichtslose Kämpfe eines erfahrenen Helden werden abgebrochen (Flucht), damit der Held erhalten bleibt.

Messung (Kampf-Arena, je Szenario beide Seiten): gegen BattleAI 71 % Siege, +0,165 Score (besser in 39 von 48 Szenarien, schlechter in 5); gegen MMAI 53 %, +0,046 (gleichauf).

**Phase 2 (Wirtschaft und Erkundung):** `Adventure/AdventureAI` spielt die Karte.
- Jeder Held plant nach jedem einzelnen Schritt neu: Der Pfadfinder der Engine (nur aufgedeckte Felder) rechnet einmal mit und einmal ohne Wächter. Ziele sind Ressourcen, Minen, Behausungen, Schätze, Fertigkeitsobjekte, Städte, Helden, Monster (plus das, was sie blockieren), Monolithen und unerkundete Gebiete. Bewertung: (Wert − erwartete Verluste) / (Reisetage + Aufwand), mit Festhalte-Bonus gegen Hin und Her.
- Objektwerte hängen nur vom sichtbaren Typ ab, nie vom versteckten Inhalt; Monstergrößen nur als Spanne („Rudel 10–19“), wie der Mensch sie sieht.
- Gebäude ohne eigene Regel bewertet FairAI über die öffentliche Tabelle ihrer Art (Schalter `tables`, noch aus): `Perception` lässt den Tabellenleser der Engine hundert Gebäude dieser Art auswürfeln – mit eigenen Würfeln, je eines pro Seite des Hunderterwürfels – und die Abenteuer-KI rechnet daraus, was ein Besuch diesem Helden jetzt im Mittel bringt (Rohstoffe nach Bedarf, Kreaturen nach dem, was die Armee wirklich gewinnt, Fertigkeiten, Zauber, Zauberpunkte, Bewegung, Artefakte, aufgedecktes Land). Das gilt für jedes Gebäude des Spiels und jeder Mod. Kreaturenbanken (`tables` = 2): Für jede mögliche Besatzung wird der Kampf in der Aufstellung der Bank simuliert; der Held geht hin, wenn sich genug der möglichen Fälle lohnen, und kehrt an der Tür um, wenn die dort angezeigten Wachen zu stark sind. Danach kennt er die Besatzung und kommt wieder, wenn die Armee reicht.
- Bietet das Spiel einer vollen Armee Kreaturen an (Bankbeute, Monster, die sich anschließen), tauscht der Held den schwächsten Trupp dagegen, wenn die neuen deutlich mehr wert sind.
- Wirtschaftsmodell: Vergleicht Wocheneinkommen mit den Kosten des gesamten Kreaturenzuwachses. Liegt Gold herum, steigen Behausungen und Truppen im Wert, Einkommen und Goldkosten sinken. Gebaut wird nach Wert/Kosten über ganze Voraussetzungsketten; fehlende Rohstoffe werden am Markt gekauft oder von den Helden gezielt gesammelt.
- Rekrutieren in die Garnison, der Haupt-Held holt die Truppen ab (eine Einheit bleibt als Wache), Kreaturen-Upgrades in der Stadt, Taverne und zweiter Held am ersten Tag.

**Phase 3 (Strategie, erste Schritte):**
- Feindliche Helden, die wir heute bis vor drei Tagen gesehen haben, bilden Bedrohungszonen. Ziele und Übernachtungsfelder in Reichweite eines stärkeren Feindes werden gemieden, bedrohte Helden ziehen sich zurück, bedrohte Städte rufen den Haupt-Helden.
- Die Gefahrenschätzung ist an echten Kämpfen kalibriert (Protokoll in jeder Partie): Feindliche Heldenarmeen sind exakt sichtbar, Heldenbonus ≈ 1,1; neutrale Wachen liegen im Mittel bei der Mitte der angezeigten Spanne. Belagerungen zählen Garnison und Mauern mit.
- Nebenhelden sammeln nahe den eigenen Städten und sind vorsichtiger; ohne Stadt wird jede erreichbare Stadt sofort angegriffen (7-Tage-Regel).

## Stand der Messungen (Partien gegen Nullkiller2, kleine Zufallskarten)

- Nach 1, 2 und 4 Wochen hat FairAI meist die größere Armee (Tag 7: 7,5k gegen 4,6k; Tag 28: etwa gleich).
- Nullkiller2 erkundet aber doppelt so viel, hält 3–4 Städte gegen 1 und gewinnt die meisten ganzen Partien. Hauptgründe: viele Helden mit Armeen, die die Karte kontrollieren und leere Städte einnehmen; FairAIs Haupt-Held wird von gleichstarken Feinden gebunden.
- Empfehlung zum Spielen: Abenteuer-KI Nullkiller2, Kampf-KI FairAI.

## Diagnose

- `FAI_PARAMS='{"safetyRatio":1.2}'` überschreibt Parameter (alle FairAI-Spieler eines Prozesses).
- `ARENA_AI_LOG=debug` für ausführliche Entscheidungs-Logs, `FAI_DUMP_MAP=1` für eine ASCII-Karte des eigenen Wissens an den Tagen 7, 14 und 28.
- `arena/game_stats.py`, `arena/battle_stats.py` werten Turniere aus.

## Fairness

Siehe `arena/README.md` (Regeln) und `arena/check_fairness.py` (automatische Prüfung).
