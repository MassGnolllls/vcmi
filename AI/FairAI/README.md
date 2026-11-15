# FairAI

Eine KI für VCMI (Heroes of Might and Magic III), die ohne Schummeln spielt: kein Aufdecken der Karte, keine Extraboni, kein Wissen, das ein Mensch auf demselben Platz nicht hätte.

## Aufbau

```
FairAI.h/.cpp        Gateway: Ereignisse und Anfragen der Engine, Zug-Thread
Status.h/.cpp        Worauf der Zug-Thread warten muss (Dialoge, Kämpfe, Bewegung)
Perception/          Fairness-Grenze: alles Wissen über fremde Objekte läuft hier durch
(Phase 1) Battle/    eigene Kampf-KI: Suche über den Kampfsimulator der Engine
(Phase 2) Adventure/ Weltmodell, Planer für Wirtschaft und Erkundung
(Phase 3) Strategy/  Bedrohungskarte, Verteidigung, Heldenketten, Angriffs-Timing
(Phase 4)            alle Gewichte als Parameter, automatisch per Selbstspiel optimiert
```

## Stand

Phase 0 (Infrastruktur): Das Gateway beantwortet alle Engine-Anfragen korrekt, hält seine Wahrnehmung aktuell (inkl. wieder verdeckter Felder) und beendet den Zug. Kämpfe übernimmt vorerst VCMIs BattleAI. Auf der Abenteuerkarte trifft FairAI noch keine Entscheidungen.

## Fairness

Siehe `arena/README.md` (Regeln) und `arena/check_fairness.py` (automatische Prüfung).
