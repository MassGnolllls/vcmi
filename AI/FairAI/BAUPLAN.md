# Bauplan: Kette, Offensive, Verteidigung, Town Portal, Mage Guilds, Wachposten, Störhelden

Stand 25.11.2026. Grundlage: Themen-Turnier vom 25.11. (96 Duelle FairAI gegen FairAI auf den Spiegelkarten, `arena/duel_reasons.py`)
und die Absprache mit dem Besitzer vom selben Abend.

## Warum

- 38 von 58 Niederlagen: die Armee stand verteilt („stärkeren Gegner gesehen, falsch reagiert" 24, „mit geteilter Armee gestellt" 14).
  Ab Tag 60 stehen 40 bis 60 % der Truppen in Städten; der Hauptheld holt sie nicht.
- 27 von 38 offenen Partien: eine Seite war an Tag 300 mindestens 1,5-fach vorn und beendete nicht. Der Feldzug verlangt dreifache
  Stärke gegen eine Schätzung, die den stärksten Gegnerhelden im Mittel 1,7-fach zu hoch ansetzt; ohne Feldzug zählt eine
  Gegnerstadt 20.000 und liegt außerhalb des Vier-Tage-Horizonts.
- Wunsch des Besitzers: Die Armee gehört an die Front, nicht in die Burg (leere Städte sind das kleinere Risiko). Ab 1,5-facher
  Stärke Minen und Dwellings des Gegners nehmen; Nachschub über eine Heldenkette; Town Portal nutzen; Mage Guilds im
  Mittel- und Spätspiel auf Stufe 5; Aufklärung; Störhelden im gegnerischen Gebiet.

## Regeln für alle Teile

1. Jeder Teil ist ein Schalter in `Adventure/Params.h`, **Standard 0**, mit Kommentar (was, warum, Messung). Ein Standard ändert
   sich erst nach einem Test und dem Wort des Besitzers.
2. Fairness: nur, was ein Mensch auf demselben Platz sieht (`Perception`). `arena/check_fairness.py` vor jedem Commit.
3. Jeder Teil schreibt die unten genannten Protokollzeilen (info), damit `arena/duel_reasons.py` sein Zielmaß zählen kann.
4. Probelauf je Teil hier: Thema an gegen aus (`--ai-params`), 2 Spiegelkarten × 2 Plätze, 150 Tage, mit `--keep-all-logs
   --ai-log debug`. Bestanden, wenn sich das Zielmaß in die gewollte Richtung bewegt, kein Befehl von der Engine abgelehnt
   wird und der Zug höchstens doppelt so lange dauert wie ohne den Teil.
5. Während ein Auftrag auf dem Zweitrechner läuft, nicht nach `fairai` pushen.

## Gemeinsame Begriffe

- **Tagesmarsch (TM)** = 1.500 Bewegungspunkte: der langsamste Tagesmarsch der Tabelle `heroes.movementPointsLand`. Die Engine
  legt die Bewegungspunkte nur zu Tagesbeginn fest; übernommene Truppen bremsen den Träger am selben Tag nicht.
- **Leichter Held**: trägt genau eine Kreatur der schnellsten Art, die wir haben (Logik von `fastKeeper`).
- **Front**: wo der Hauptheld am Zugende steht. **Sammelstadt**: eigene Stadt mit dem kürzesten bekannten Landweg zur Front.
- **Ehrliche Gegnerschätzung** `enemyReference(feld)` – ersetzt den Aufschlag „× 3 % je Tag seit der Sichtung":
  je Gegnerheld mit Sichtung der letzten 21 Tage: gezeigte Armee × `heroFactor` + Zuwachs. Zuwachs = (Tage seit der Sichtung / 7)
  × halbes Wochenwachstum seiner bekannten Städte (je Stadt: unser eigenes mittleres Wochenwachstum je Stadt in Armeewert),
  höchstens + 50 % der gezeigten Armee. Genommen wird der stärkste. Liegt `feld` höchstens einen TM vor einer bekannten
  Gegnerstadt, kommt deren Garnison dazu (wie gesehen; ungesehen: ein Wochenwachstum).
- **Vorsprung** = `fieldStrength(Hauptheld)` / `enemyReference(feld)`.

## Heldenbudget (höchstens acht Helden auf der Karte, `perPlayerOnMapCap`)

Neue Funktion `assignRoles()` am Anfang von `playTurn()` (nach `chooseMainHero()`), einmal je Tag. Vergabe in dieser Reihenfolge,
solange Helden da sind bzw. Gold für neue reicht:

1. Hauptheld
2. Kettenglieder (Teil 1; Anzahl nach Weglänge)
3. Störheld 1 (Teil 7)
4. Wachposten der Sammelstadt (Teil 6)
5. Störheld 2
6. weitere Wachposten
7. Rest: Sammler wie bisher (`collectorsRoam`, `scouts`)

Mit Kette sind die Kettenglieder die Sammler ihres Abschnitts. Mit Störhelden werden die bisherigen `scouts` zu Störhelden,
sobald gegnerisches Gebiet bekannt ist (nicht zusätzlich). Helden, die heute „aus der Garnison bewachen", sind der Vorrat für
Kette, Wachposten und Störhelden.

Heutiger Zugablauf (`AdventureAI::playTurn`, ab Zeile ~870): `manageTowns(false)` → `hireHeroes()` → Hauptheld (`playHero`) →
übrige Helden nach Stärke: Kurier (`supply`), zweite Anführer, Stadthelden, dann `collectorsRoam` Sammler, `scouts` Späher, der
Rest sitzt in der Garnison → `hireHeroes()`, `manageTowns(true)`.

---

## Teil 1 – Heldenkette mit Wochenrunde (`relayChain`)

Ersetzt `supplyRuns`, `couriers`, `muster`, `refillShare` (bei `relayChain` an wirken sie nicht).

**Liefertag** ist Tag 1 jeder Woche, außerdem jeder Tag, an dem in der Sammelstadt Truppen warten, die dem Haupthelden mindestens
15 % seiner Armee bringen (`transferGain`), frühestens drei Tage nach der letzten Lieferung.

**Ablauf am Liefertag – vor dem Zug des Haupthelden:**
1. `manageTowns` kauft in allen Städten alle wartenden Kreaturen (an diesem Tag Truppen vor Gebäuden).
2. Glied 0 steht in der Sammelstadt, übernimmt alles, was der Hauptheld brauchen kann (`transferableValue`), und läuft zu Glied 1.
   Tausch (`onHeroExchange`): Glied 1 bekommt die Ladung, Glied 0 behält seine eine schnelle Kreatur. Glied 1 läuft zu Glied 2
   und so weiter; das letzte Glied läuft zum Haupthelden und übergibt.
3. Dann zieht der Hauptheld – mit der neuen Armee.

**Posten.** Der Weg Sammelstadt → Front kommt vom Pfadfinder (bekannte Felder, Wachen gelten als Sperre). Glied k steht am Zugende
bei k TM auf diesem Weg. Anzahl n = aufgerundet (Weglänge / TM). Ein Posten steht nicht auf einem bewachten Feld und möglichst
nicht in Reichweite eines Gegnerhelden; sonst rückt er einen halben TM zurück. Nach einer Lieferung steht jedes Glied einen TM
zu weit vorn und läuft am nächsten Tag auf seinen Posten zurück.

**Kurze Kette.** Reichen die Helden nicht, kommt der Hauptheld der Kette am Liefertag entgegen: das vorderste Glied ist für ihn
ein Ziel im Wert von `transferGain`. Die Ladung übernachtet nie bei einem Glied im Freien, wenn sie mehr als ein Viertel der
Hauptarmee wert ist und `threatAt(feld) > 0`.

**Zubringer.** Truppen anderer Städte holt an den Tagen 2 bis 7 das Glied, dessen Posten höchstens einen TM von der Stadt
entfernt liegt, und trägt sie zur Sammelstadt oder auf seinen Posten; sie gehen mit der nächsten Lieferung. Was nicht in die
sieben Plätze des Haupthelden passt, bleibt in der Sammelstadt.

**Wochenrunde.** An den übrigen Tagen besucht ein Glied im Umkreis von einem halben TM um seinen Posten Windmill, Water Wheel,
Mystical Garden (Werte wie bisher) und **eigene Dwellings** (neu: `valueOf` gibt für eigene Dwellings bisher 0; Wert = was die
Wochenkreaturen dem Haupthelden bringen abzüglich Kaufpreis, einmal je Woche, nur wenn das Gold nach dem Truppenkauf der Städte
reicht) und steht am Zugende wieder auf dem Posten.

**Folgen.** Mit Kette läuft der Hauptheld nicht mehr selbst heim, um Truppen zu holen – außer es kam zehn Tage lang keine
Lieferung zustande. Städte halten nichts zurück (`keepGarrison`, `garrisonShare`, `towerGuard` wirken nicht), außer Teil 3
fordert Truppen an.

**Einbau.** `planChain()` (Posten, Liefertag) und `playChain()` vor `playHero(mainHero)`; die Übergabe aus dem Code der
Versorgungsläufe (`planSupplyRun`, `SupplyRun`, `transferGain`, `recruitInTown`, `onHeroExchange`) wiederverwenden.

**Protokoll.** `chain day N: n links, load X from <Stadt>, delivered Y to <Held> (short by k links)` · `chain post <Held> at (x y z)`
· `chain round: <Held> buys N <Kreatur> at own dwelling (x y z)`.

**Zielmaß.** Truppenanteil in Städten an Tag 90 und vor der ersten großen Schlacht: unter 20 % (heute 43 bis 55 %).

---

## Teil 2 – Offensive in zwei Stufen (`advance`)

Ersetzt den Auslöser des Feldzugs (`leaderFocus` / `campaignEdge`), nicht dessen Ausführung.

| Vorsprung | Zustand | Verhalten des Haupthelden |
|---|---|---|
| unter 1,5 | Aufbau | wie bisher im eigenen Gebiet; die Kette vereint die Armee |
| ab 1,5 (aus unter 1,2) | Vorstoß | ins gegnerische Gebiet |
| Kampfprognose reicht für die Mauern | Städte | Städte nehmen, die schwächste zuerst |

**Vorstoß.**
- Horizont: Ziele bis zwölf Tage entfernt (wie im Feldzug), nicht vier.
- Werte: gegnerische Mine = doppelter Wert der eigenen Mine (sie fehlt ihm und nützt uns; heute 1,5-fach), gegnerisches
  Dwelling ebenso. Gegnerheld mit höchstens halber Stärke: seine Armee in Gold + 2.500. Stadt ohne Held: Stadtwert des Feldzugs.
- Sicher ist ein Feld für den Haupthelden, wenn `enemyReference(feld)` × 1,2 ≤ `fieldStrength`.
- Vorn bleiben: kein Heimweg für Truppen (Kette). Unter gleich guten Endfeldern das auf dem Kettenweg (+ 10 %).
- Taucht eine Armee auf, gegen die der Vorsprung unter 1,0 fällt: zurück entlang der Kette, dem Nachschub entgegen (Teil 3).

**Städte.** Eine Stadt mit Held oder Garnison wird angegriffen, wenn die Kampfprognose (bestehende Belagerungslogik,
`heroForecast`) einen Sieg mit höchstens 35 % Verlust zeigt.

**Bis zum Ende.**
- Kein Abschalten, weil drei Wochen lang kein Gegnerheld gesehen wurde: dann gilt der Vorsprung als unendlich.
- Die letzte bekannte Stadt eines Gegners zählt fünffach.
- Lebt der Gegner und ist keine seiner Städte bekannt, suchen Hauptheld und Störhelden sie mit Vorrang (Logik von `seekTowns`
  und der Suche aus `breakout` 4 – nur in diesem Fall, nicht ab der dritten Woche).

**Einbau.** Feldzug-Block in `AdventureAI.cpp` (Kommentar „Campaign: our main hero is clearly the strongest force …"),
`valueOf` für Gegnerobjekte, die Sicherheitsprüfung (`unsafe`), die Berechnung von `oppositionHeroes` („x … for the days since").

**Protokoll.** `advance on/off (main A, enemy reference B = seen C + growth D + garrison E, edge F)` · `advance: <Held> flags enemy
mine/dwelling at (x y z)` · `siege of <Stadt>: forecast loss L % -> attack/wait`.

**Zielmaß.** Vom Haupthelden genommene Minen des Gegners; Tage von 1,5-fachem Vorsprung bis zum Sieg; Anteil offener Partien.

---

## Teil 3 – Verteidigung: Reichweite und Reaktion (`regroup`, zusammen mit `threatReach` 1)

Zuerst die Diagnose: `results-keep/themes/reasons_all.json` listet die 24 Fälle „gesehen, falsch reagiert" (Partie, Tag, Held).
Drei davon im Protokoll nachvollziehen – welche Regel hielt den Helden, wo er stand.

**A. Zugende im Feld** (Hauptheld und jeder Held mit Armee ab `heavyPartValue`).
Ein Ziel wird verworfen, wenn das Zielfeld unsicher ist (`threatAt > defenceAt`) und danach kein sicheres Endfeld mehr
erreichbar ist („no way back"). Ist schon der Startpunkt unsicher und nichts Sicheres erreichbar: dem Nachschub entgegen
(nächstes Kettenglied, Sammelstadt), nicht „so weit weg wie möglich".

**B. Bedrohte Stadt** (bekannte Armee in Reichweite, stärker als das, was die Stadt hält). Die erste Möglichkeit, die reicht
(Summe × Mauerfaktor ≥ Bedrohung):
1. Truppen aus Nachbarstädten und von Posten, die die Stadt heute erreichen (Kette rückwärts);
2. der Hauptheld erreicht die Stadt heute, zu Fuß oder mit Town Portal (Teil 4);
3. beides zusammen;
4. reicht nichts: räumen. Gold der Stadt in Truppen umsetzen (`threatSpend`-Logik), Held und Truppen verlassen sie in Richtung
   der stärksten eigenen Gruppe. Nicht bei der letzten Stadt (`lastStand` bleibt).

**C. Held in einer Stadt, schwächer als die Bedrohung, eigene Truppen insgesamt stärker** (16 der 24 Fälle): er geht zu den
Truppen – in die Stadt mit dem größten erreichbaren Bestand – oder holt sie nach B.1, statt sitzen zu bleiben.

**Protokoll.** `regroup at <Stadt>: threat X, holds Y -> option N (…)` · `no way back: <Held> drops target at (x y z)`.

**Zielmaß.** Niederlagen „gesehen, falsch reagiert" und „geteilte Armee gestellt"; große Armeen im Feld gestellt.

---

## Teil 4 – Town Portal (`townPortal`)

Heute wirkt FairAI auf der Karte nur Fly und Water Walk (`moveSpells`). Regeln des Zaubers aus der Spielkonfiguration lesen
(`config/spells/adventure.json`: 300 Bewegungspunkte nötig, mit Expert Earth Magic 200; Stadtwahl ab Advanced; ohne Wahl die
nächste eigene Stadt – Zielregel in der Engine nachlesen) und Zauberpunkte über `spell->getCost(stufe)`.

Einsätze, geprüft zu Beginn des Heldenzugs und nach jedem Ziel, in dieser Reihenfolge:
1. **Verteidigung:** Teil 3 B.2.
2. **Flucht:** der Held steht unsicher im Feld und erreicht kein sicheres Feld.
3. **Abholen:** in einer eigenen Stadt warten Truppen im Wert von mindestens 30 % seiner Armee, und die Kette bringt sie nicht
   binnen zwei Tagen. Im Vorstoß nur, wenn die Stadt näher an der Front liegt als der Held.
4. **Zauber und Zauberpunkte:** eine Stadt, deren Mage Guild Zauber hat, die er lernen kann; oder weniger als 30 % Zauberpunkte
   vor einer erwarteten Schlacht.

**Reserve.** Solange eine Bedrohung für eine eigene Stadt bekannt ist, bleiben die Zauberpunkte für einen Town Portal
unangetastet (Kampf-KI: unterhalb der Reserve kostet ein Zauberpunkt das Dreifache, über `setManaUrgency`).

**Protokoll.** `<Held> casts Town Portal to <Stadt> (defence|escape|pickup|spells)`.

**Zielmaß.** Einsätze je Partie nach Zweck; gehaltene bedrohte Städte.

---

## Teil 5 – Mage Guilds bis Stufe 5 ab der Spielmitte (`lateGuilds`, baut auf `magicFocus` auf)

`magicFocus` brachte die Zauber (6,5 statt 1,4 der Stufen 4 und 5 an Tag 147), kostete aber früh Truppen (Armee an Tag 42
um 12.000 kleiner).

- Stufen 1 und 2 wie bisher.
- Stufen 3 bis 5 erst ab Tag 43 und nur aus Gold, das an diesem Tag nach dem Kauf aller wartenden Truppen übrig ist.
  Zuerst in der Sammelstadt, dann in der Stadt, die der Hauptheld als nächste erreicht.
- Der Hauptheld holt neue Zauber, wenn der Umweg höchstens einen Tag kostet oder mit Town Portal.
- Dabei klären, warum `idleBuild` das brachliegende Gold nicht senkt (im Turnier 55.000 gegen 60.000 an Tag 90).

**Zielmaß.** Mindestens fünf Zauber der Stufen 4 und 5 an Tag 147 bei unveränderter Armee an Tag 42 (± 5.000).

---

## Teil 6 – Wachposten (`watchPosts`)

Helden ohne Aufgabe sitzen heute in der Garnison. Stattdessen:

- Je eigene Stadt (Sammelstadt und Hauptstadt zuerst) ein leichter Held auf dem kürzesten bekannten Weg vom Gegner zur Stadt
  – von der nächsten bekannten Gegnerstadt oder von dem Tor oder Monolithen, durch den der Gegner kam –, etwa 24 Felder vor
  der Stadt, so gewählt, dass der Weg im Sichtradius liegt.
- Steht am Zugende ein Gegnerheld in Reichweite, weicht der Posten seitlich aus und hält möglichst die Sicht. Ein verlorener
  Wachposten kostet 2.500 Gold, nicht mehr.
- Kettenglieder zählen als Wachposten ihres Wegabschnitts.

**Protokoll.** `watch post <Held> at (x y z) for <Stadt>`; bei jedem Angriff auf eine eigene Stadt die Zeile, die es für Armeen
schon gibt („our reach model had it … (sighting N day(s) old)").

**Zielmaß.** Anteil der Angriffe auf eigene Städte und große Armeen, bei denen der Angreifer am Vortag gesehen war; Städte, die
ohne Kampf verloren gingen.

---

## Teil 7 – Störhelden (`raiders`)

Ein Störheld, sobald gegnerisches Gebiet bekannt ist (eine Mine oder Stadt des Gegners und ein Weg dorthin) und 2.500 Gold über
der Reserve liegen. Ein zweiter bei brachliegendem Gold und freiem Heldenplatz. Nach einem Verlust höchstens ein neuer je Woche.

- **Armee:** eine Kreatur der schnellsten Art.
- **Ziele – nur diese:** unbewachte Minen des Gegners (doppelter Minenwert), seine Dwellings, freie Schätze und Rohstoffe in
  seinem Gebiet. Aufklärung: unerkundete Felder dort, Sicht auf seine Städte (Garnison ablesen) und auf seine Hauptarmee
  (Sichtung erneuern, wenn sie älter als drei Tage ist).
- **Risiko:** Ziele werden nicht verworfen, weil sie „unsicher" sind – nur wenn ein sichtbarer Gegnerheld das Ziel vor uns
  erreicht oder auf dem Weg steht. Zugende: unter den nach dem letzten Ziel erreichbaren Feldern das mit der geringsten
  Bedrohung (hier zählt jeder Gegnerheld, auch ein schwacher); gern ein Sanctuary oder ein eigenes Kettenglied.
- Kämpft nie, greift nichts an, nimmt nichts Bewachtes.

**Protokoll.** `raider <Held>: flags enemy mine/dwelling at (x y z)` · `raider <Held> lost on day N`.

**Zielmaß.** Von Störhelden genommene Minen des Gegners; Einkommensverhältnis an Tag 90 und 150; Alter der letzten Sichtung
der gegnerischen Hauptarmee; verlorene Störhelden.

---

## Reihenfolge des Baus

1. Kette (Teil 1) – alles andere hängt daran
2. Offensive (Teil 2)
3. Verteidigung (Teil 3)
4. Town Portal (Teil 4)
5. Mage Guilds (Teil 5)
6. Störhelden (Teil 7)
7. Wachposten (Teil 6)

Nach jedem Teil: Probelauf, kurzer Bericht an den Besitzer.

## Der Test danach

**Basis beider Seiten:** die Standards von heute, `threatReach` wieder 0 (die Reichweite wird nur noch mit ihrer Reaktion
getestet), `spellSearch` 2.

**Themen-Turnier** (96 Partien, `--duel-plan`): 13 Themen und zwei Leerspalten (Themen ohne Schalter – ihre Wertung muss bei
0,5 liegen und zeigt, wie groß der Zufall ist).

| Nr. | Thema | Schalter |
|---|---|---|
| 1 | Heldenkette | `relayChain` |
| 2 | Offensive | `advance` |
| 3 | Verteidigung | `regroup`, `threatReach` 1 |
| 4 | Town Portal | `townPortal` |
| 5 | Mage Guilds | `magicFocus`, `lateGuilds` |
| 6 | Wachposten | `watchPosts` |
| 7 | Störhelden | `raiders` |
| 8 | heroChoice | `heroChoice` |
| 9 | skillChoice | `skillChoice` |
| 10 | artifactCare | `artifactCare` |
| 11 | primaryByArmy | `primaryByArmy` |
| 12 | Kampf gegen Wachen | `kitePlan`, `latePlan`, `fightForecast` 2, `forecastPlans` |
| 13 | Gegner einschätzen | `heroForecast`, `freshHeroes`, `spellForecast` |
| 14, 15 | *leer* | – |

**Zusammenspiel.** Die Offensive braucht die Kette, Town Portal braucht die Mage Guilds. In diesem Plan spielt jede Seite gegen
ihr Gegenstück; ein Zusammenspiel zweier Themen hat deshalb keine eigene Spalte, es steckt je zur Hälfte in beiden Wertungen.
Dafür gibt es für jedes Paar zwei Sorten Partien, je 48: „beide gegen keins" und „das eine gegen das andere". `duel_reasons.py`
bekommt eine Tabelle je Paar: Siege, Gründe und Zielmaße der Seiten mit beiden, mit nur einem und mit keinem der zwei Themen.

Auswertung wie am 25.11.: Zielmaß, Niederlagengrund, Wertung (`duel_effects.py`, `duel_reasons.py`; die neuen Protokollzeilen
dort einlesen).

**Ankerlauf gegen den Maßstab des Projekts:** 24 Partien FairAI gegen Nullkiller2 + MMAI auf den Spiegelkarten mit vollen
Protokollen, einmal mit der Basis und einmal mit der neuen Version; die Gründe für FairAIs Seite mit `duel_reasons.py`.
Das Duell misst die Stärke gegen einen Gegner mit starkem Haupthelden und geballter Armee, nicht das Ziel von 95 %.

Nicht mehr im Test: Zweite Armee, Gegner suchen, Städte zählen, Sicher stehen (im Turnier schädlich); Früher angreifen und
Truppen sammeln (in den Teilen 1 und 2 aufgegangen); Früh kämpfen, Wege und Ziele, Gold nutzen (wirkungslos).

## Stand des Baus

### Teil 1 – Heldenkette (`relayChain`), gebaut am 25.11.2026

Gebaut wie geplant mit diesen Abweichungen (jede aus einem Probelauf heraus):

- **Sammelstadt = die Stadt, in der am meisten wächst** (nicht die frontnächste): die schwerste Last bekommt den schnellen
  Weg. Eine einmal gewählte Sammelstadt bleibt, solange dort vier Fünftel des Besten wachsen.
- **Posten** alle 1.350 Bewegungspunkte (`chainSpacing`) auf dem Weg Sammelstadt → Hauptheld, von der Sammelstadt aus
  gezählt, nicht auf Feldern in Reichweite bekannter Gegner. Die Kette ist nur aktiv, wenn der Hauptheld weiter als einen
  Posten entfernt ist; sonst holt er selbst wie bisher.
- **Lieferung nur, wenn es heute einen Weg gibt:** vor dem Laden wird von den wirklichen Standorten aus geprüft, ob die
  Last über Glieder mit vollem Tagesmarsch bis zum Haupthelden kommt, jeder Schritt in der Tagesreichweite des Helden, der
  ihn geht (`chainRoute`). Gibt es keinen Weg, bleibt die Last in der Stadt (und der Hauptheld darf sie holen wie vor der
  Kette). Erste Fassung ohne diese Prüfung: 17 von 26 Läufen kamen nicht am selben Tag an, ein Drittel der Truppen stand bei
  Stufe-1-Helden im Freien. Mit der Prüfung: 81 von 84 Läufen am selben Tag.
- **Liefertage:** erster Tag der Woche; am zweiten bis fünften Tag, wenn in der Stadt mindestens 15 % der Hauptarmee
  warten (`chainShare`) und der letzte Lauf drei Tage her ist.
- **Geladen wird nur, was der Hauptheld führen kann** (`usefulSlots`): seine eigenen Arten, neue Arten für freie Plätze,
  und was klar besser ist als sein schwächster Trupp. Der Rest bleibt in der Stadt. Bei der Übergabe geht alles Brauchbare
  weiter (`relayTransfer`), was der Empfänger an Unbrauchbarem trägt, geht mit dem Geber zurück (`relayReturn`).
- **Glieder reisen leicht:** ein Glied behält ein Wesen seiner schnellsten Art, aber keines, das für sich eine Armee ist
  (`keeperSlot`; ein Glied hatte seinen einzelnen Archangel als „schnellstes Wesen" wochenlang hin- und hergetragen). Wer
  in einer Stadt übernachtet, lässt morgens die Stadttruppen dort (`leaveTroops`). Glieder werden nie Hauptheld.
- **Leine:** ein Glied arbeitet tagsüber höchstens einen halben Tagesmarsch (`chainLeash`) von seinem Posten und steht
  abends wieder dort; eigene Dwellings gehören zur Runde, wenn der Hauptheld die Art führen kann.
- **Nebenstädte:** die zwei mit dem meisten Wachstum bekommen ein Glied, wenn nach den Posten Helden übrig sind; ihre Last
  geht nur, wenn sie am selben Tag die Kette oder den Haupthelden erreicht.
- **Heldenzahl:** bis zu sieben Glieder (`chainMaxLinks`); bis Tag 21 bleiben die Kundschafter Kundschafter, Helden für
  die Kette werden ab Tag 15 angeworben.
- Nicht gebaut: das Entgegenkommen des Haupthelden bei zu kurzer Kette (er wird nur über den Zielwert angezogen);
  `towerGuard` bleibt, wie es ist (ein Wesen).

Protokollzeilen: `chain of N links (P posts on the road of W wanted …, short by K), hub …`, `chain post <hero> at …`,
`chain day N: n links, load X from <town>, delivered Y to <hero> (…); handed on H times: A > B > …`, `chain day N: X waits
in <town>, no way along the chain to the main hero today (…): no run`, `chain round: <hero> buys …`; für beide Seiten,
mit oder ohne Kette, zweimal je Woche: `troops: main army A, in our towns U for the main hero to take, garrisons G, heroes
in towns H, other heroes in the field F`.

Probelauf (2 Spiegelkarten × 2 Plätze, 150 Tage, Kette gegen ohne): 14 bis 26 Lieferungen je Partie, 525.000 bis 674.000
Armeewert, im Mittel 2,3 bis 3,3 Übergaben, keine verweigerten Befehle, Zugdauer unverändert. Wertung 0,88 / 0,00 / 1,00 /
0,15 – vier Partien sagen über die Stärke nichts.

**Befund, der das Zielmaß von Teil 1 umwirft:** Von den Truppen in den Städten kann der Hauptheld nur einen kleinen Teil
führen. Seiten ohne Kette in zwei Probeläufen (8 Partien, Tag 22 bis 150, zwei Messungen je Woche): in den Städten steht
im Median das 1,2-fache der Hauptarmee (0,7 bis 1,8), davon kann der Hauptheld im Median 12 % seiner Armee übernehmen
(0 bis 68 %) – er hat sieben Plätze, und die Städte einer Seite haben vier bis sechs verschiedene Stadttypen. „Unter 20 % der Truppen in Städten" ist mit einem Helden nicht erreichbar. Die Kette bringt dem
Haupthelden, was er führen kann, am Tag des Kaufs; der große Rest braucht eigene Helden oder bleibt Besatzung.

### Ankerlauf der Basis gegen Nullkiller2 + MMAI (Auftrag 035, 24 Partien)

15 Siege, 0 Niederlagen, 8 Partien am Tageslimit (Truppenanteil 0,65 bis 0,98), 1 Partie ohne Ergebnis (Nullkiller2 blieb
an Tag 120 in seiner eigenen Heldenkette hängen). Wertung 0,966.

Die 8 offenen Partien: in 7 wechseln die Städte hin und her. FairAI gewann dort 16 bis 57 Belagerungen je Partie (alle),
Nullkiller2 nahm 14 bis 39 Städte zurück (alle ohne Helden von uns in der Stadt; Angreifer im Median 182.000, Besatzung
im Median 41.000, 2,5 Tage nach unserer letzten Eroberung). Nullkiller2 hält dabei 5 bis 8 Helden; FairAI hat insgesamt
das 2- bis 20-fache an Truppen, zwei Drittel davon in den eigenen Städten. In der achten Partie hatte der Gegner eine
Stadt, die FairAI 200 Tage lang nicht fand.

Folgerung: gegen Nullkiller2 fehlt nicht die Stärke des Haupthelden, sondern das Halten des Eroberten und das Stellen der
mittleren Helden. Dafür sind genau die Truppen da, die der Hauptheld nicht führen kann: als Besatzung der eroberten Städte
und als zweite und dritte Armee. Vorschlag: als eigener Teil vor Teil 2 (siehe Bericht vom 25.11.).

### Teil 1b – Wardens: Helden für die Truppen, die der Hauptheld nicht führen kann (`wardens`), gebaut am 25./26.11.2026

Auftrag des Besitzers vom 25.11. („passt für mich, bitte so umsetzen"): Besatzung und zweite Armeen aus den Truppen, die
der Hauptheld nicht führen kann, vor der Offensive. Standard aus; `wardens` = Höchstzahl (Test mit 2).

- **Zweite Linie** einer Stadt (`secondLine`): was dort steht und was dort zu kaufen ist, soweit der Hauptheld es nicht
  führen kann (`usefulSlots`, `mainCanUse`). Nichts, was der Hauptheld brauchen kann: das bleibt für ihn und die Kette.
- **Entstehung** (`formWardens`, jeden Morgen nach Kette und Hauptheld, ab Tag 29): die Stadt mit der größten zweiten Linie,
  wenn sie mindestens 25 % der Hauptarmee wert ist (`wardenShare`, mindestens 30.000). Ein Held, der dort steht und kein
  Kettenglied ist, übernimmt sie; steht keiner da, wird dort einer angeworben. Wo ein Warden in einer eigenen Stadt steht,
  kauft und nimmt er jeden Morgen, was für ihn gewachsen ist (`takeSecondLine`). Er bleibt Warden bis zur Hälfte des
  Werts, mit dem er antrat.
- **Zwei Rollen nach Stärke:** Ein Warden, der das 1,3-fache des stärksten Gegnerhelden der letzten 60 Tage hat (gesehene
  Armee × Heldenfaktor, 3 % Zuwachs je Tag bis zum Doppelten), ist **Jäger**: er wird wie ein Anführer gespielt, geht auf
  Gegnerhelden und Städte, steht zwischen den Kämpfen in der zuletzt eroberten oder bedrohten Stadt ohne eigenen Helden
  („hold"). Ein schwächerer ist **Hüter**: nachts hinter den Mauern, tagsüber Tagesausflüge, Umzug in umkämpfte Städte,
  und er geht, bevor die Armee kommt, die ihn schlägt – in eine Stadt, die hält, sonst ins freie Feld aus dem Weg.
- **Bedrohung nach wirklicher Reichweite:** Wardens urteilen immer nach dem Reichweitenmodell (`reachView`), auch wenn
  `threatReach` aus ist. Erste Fassung ohne das: vier Wardens mit 180.000 bis 270.000 wurden in Städten geschlagen, die
  der Gegner aus 20 Feldern Entfernung erreichte.
- **Arbeitsteilung:** Eine Stadt, die der Gegner uns wieder abgenommen hat (`townLostDay`), zählt für Wardens doppelt und
  für den Haupthelden ein Viertel, solange ein Warden in 30 Feldern Nähe ist: der Hauptheld soll weiter, zu den Städten,
  aus denen die gegnerischen Armeen kommen.
- **Heldenzahl:** Die Plätze der Wardens bleiben frei: die Kette nimmt dann höchstens 8 − 1 − `wardens` Helden.
- Protokoll: `warden <hero> leads the second line of <town>: army A (needed B; …)`, `warden <hero> hunts / keeps to our
  towns (…)`, `warden <hero> takes on troops in <town>`, `warden <hero> leads no longer`, dazu die Zeilen der Stadthelden
  (`cannot hold`, `moves its army`), und in `troops:` am Ende `wardens N with X`. Auswertung: `arena/ward_scan.py`.

Probeläufe gegen Nullkiller2 + BattleAI (lokal, einzelne Partien, 300 Tage; MMAI läuft hier nicht): Wardens entstehen ab
Tag 39 bis 71 und werden groß – im Median 400.000 bis 720.000, das 0,5- bis 1,5-fache der Hauptarmee. Partien gegen einen
schwachen Gegner enden früher (Factory: Tag 197 statt offen; Inferno: Tag 138), das Hin und Her der Städte bleibt aber
(11 bis 20 verlorene Städte je Partie, keine der Rückeroberungen traf auf einen Helden von uns).

**Befund aus diesen Partien: gegen Nullkiller2 mit BattleAI verliert auch die Basis.** Von vier Partien der Basis auf
Rampart und Inferno: drei Siege, eine Niederlage (Rampart, Tag 218). In den Partien, die schlecht ausgingen, hatte der
gegnerische Hauptheld einen Heldenfaktor von 2,8 bis 4,4 gegen 1,2 bis 2,5 bei uns, bei etwa gleicher Armee (z. B. 733.000
× 1,76 gegen 809.000 × 3,75: wir verloren 74 %, er 28 %). Drei getrennte Armeen (Hauptheld und zwei Wardens, zusammen das
2,7-fache der gegnerischen Armee) wurden dann einzeln geschlagen. Der Ankerlauf gegen Nullkiller2 + MMAI (0 Niederlagen)
zeigt das nicht: MMAI kämpft deutlich schwächer als BattleAI. Der Maßstab des Projekts ist Nullkiller2 + BattleAI – dafür
laufen jetzt eigene Ankerläufe (Basis, Wardens, und die vier Schalter des Themas „Held").
