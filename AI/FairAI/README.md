# FairAI

Eine KI für VCMI (Heroes of Might and Magic III), die ohne Schummeln spielt: kein Aufdecken der Karte, keine Extraboni, kein Wissen, das ein Mensch auf demselben Platz nicht hätte.

## Aufbau

```
FairAI.h/.cpp        Gateway: Ereignisse und Anfragen der Engine, Zug-Thread
Status.h/.cpp        Worauf der Zug-Thread warten muss (Dialoge, Kämpfe, Bewegung)
Perception/          Fairness-Grenze: alles Wissen über fremde Objekte läuft hier durch
Combat/              eigene Kampf-KI (Phase 1): schneller Kampfsimulator + Vorausschau-Suche
(Phase 2) Adventure/ Weltmodell, Planer für Wirtschaft und Erkundung
(Phase 3) Strategy/  Bedrohungskarte, Verteidigung, Heldenketten, Angriffs-Timing
(Phase 4)            alle Gewichte als Parameter, automatisch per Selbstspiel optimiert
```

## Stand

**Phase 0 (Infrastruktur):** Das Gateway beantwortet alle Engine-Anfragen korrekt, hält seine Wahrnehmung aktuell (inkl. wieder verdeckter Felder) und beendet den Zug. Auf der Abenteuerkarte trifft FairAI noch keine Entscheidungen.

**Phase 1 (Kampf):** FairAI kämpft mit eigenem Kampfmodul (`Combat/`):
- `CombatSim`: eigener, schneller Kampfsimulator. Schaden pro Kreatur für jedes Einheitenpaar kommt aus der Schadensberechnung der Engine (Heldenwerte, Spezialitäten, Zauberwirkungen), die Abläufe (Zugreihenfolge, Bewegung, Vergeltung, Doppelangriffe, Schützen, Munition, Blockieren) simuliert FairAI selbst mit Erwartungswerten.
- `FairBattleAI`: Für jede Einheit werden alle legalen Angriffe, Schüsse, einige Bewegungsziele, Warten und Verteidigen bis zum Rundenende durchgespielt; gewählt wird die Aktion mit der besten Materialbilanz. Ergebnisse von Angriffen mit Sonderfähigkeiten liefert die Engine-basierte Auswertung.
- Heldenzauber, Katapult, Erste-Hilfe-Zelt, Berserker und zaubernde Kreaturen übernehmen vorerst Bausteine von VCMIs BattleAI (in FairAI einkompiliert).

Messung (Kampf-Arena, je Szenario beide Seiten): gegen BattleAI 71 % Siege und +0,165 Score auf 48 nicht zum Tuning genutzten Szenarien (besser in 39, schlechter in 5); gegen MMAI 53 % Siege, +0,046 (gleichauf, Unterschied nicht signifikant).

## Fairness

Siehe `arena/README.md` (Regeln) und `arena/check_fairness.py` (automatische Prüfung).
