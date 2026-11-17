# FairAI – Anforderungen des Nutzers

Diese Datei hält fest, was der Nutzer von der fertigen KI verlangt. Sie ist die Prüfliste: Bevor
FairAI als fertig gilt, wird jeder Punkt hier einzeln abgehakt.

**Pflege**
- Jede neue Anmerkung des Nutzers wird hier eingetragen, mit Datum und möglichst in seinen Worten.
- Nichts wird gelöscht. Erledigtes bekommt den Stand „erfüllt" und bleibt stehen.
- Der Stand je Punkt: **erfüllt** (im Standard und gemessen oder nachgeprüft), **im Test** (geschrieben,
  als Schalter, Messung läuft), **teilweise**, **offen**.
- Was die KI heute tatsächlich kann, steht in `README.md`; Messungen und Reihenfolge stehen in der
  Roadmap des Projekts.

Letzte Änderung: 17.11.2026

---

## Leitsätze (gelten für alles Weitere)

| Nr. | Anforderung | Seit | Stand |
|---|---|---|---|
| L1 | **Nicht schummeln.** Kein Maphack, keine Extraboni; nur Wissen, das ein Mensch auf demselben Platz hätte. | 15.11. | erfüllt (Prüfung `arena/check_fairness.py` vor jedem Stand) |
| L2 | **So stark und klug wie möglich** – auf allen gewöhnlichen Karten, nicht nur auf einer. | 15.11./16.11. | offen (siehe Zielmarke Z1) |
| L3 | **Alles ist situativ.** Nichts wird pauschal getan oder pauschal gelassen. Jede Option wird in Betracht gezogen, aber nur ausgeführt, wenn sie in dieser Lage etwas bringt. „Nur weil man alle Gebäude und Optionen in Betracht ziehen soll, muss man nicht alles machen." | 17.11. | teilweise |
| L4 | **Jeden Aspekt des Spiels vorher genau untersuchen** und der KI klarmachen, welchen Nutzen und welche Gefahren jede Entscheidung mit sich bringt. | 17.11. | offen (Katalog der Einheitenfähigkeiten in Arbeit) |
| L5 | **Nach jedem Schritt neu bewerten**, wenn neue Informationen auftauchen. | 15.11. | erfüllt |
| L6 | **Nebel, der neu entsteht** (um Nekropolen, Cover of Darkness), verstehen und klug darauf reagieren. | 15.11. | teilweise (wieder verdecktes Land gilt als veraltet; auf Feldzug wie unerkundet) |
| L7 | Eine KI für Abenteuerkarte **und** Kampf. | 15.11. | erfüllt (Belagerungen und Teile der Zauberwahl spielt noch VCMIs Kampflogik, siehe K9, M1) |

## Ziel und Umfang

| Nr. | Anforderung | Seit | Stand |
|---|---|---|---|
| Z1 | **Zielmarke:** mindestens 95 % aller Partien gegen die Standard-KI gewinnen, möglichst auch 95 % gegen Nullkiller2 mit der Kampf-KI MMAI. | 17.11. | offen (mittlere Landkarten: rund 45 % Siege, 20 % Niederlagen, Rest offen nach 150 Tagen) |
| Z2 | Alle gewöhnlichen Karten mit dem Ziel „alle Gegner besiegen": winzig bis riesig, Land und Inseln, auch brandneue Zufallskarten. Keine Spezialkarten mit Sonder-Siegbedingung (z. B. Lex) – die kommen danach. | 16.11. | teilweise (läuft überall; gemessen und gut nur auf kleinen und mittleren Landkarten) |
| Z3 | 1 gegen 1, jeder gegen jeden, mehrere Gegner, Teams. **Verbündete korrekt erkennen.** | 16.11. | teilweise (Verbündete sind keine Ziele; Zusammenspiel im Team fehlt) |
| Z4 | **Alle Fraktionen einschließlich HotA**: Cove, Factory, Bulwark. | 16.11. | teilweise (alle zwölf spielen ohne Absturz; Stärke je Fraktion sehr verschieden) |
| Z5 | Die Entwicklung soll nicht Monate dauern. | 15.11. | – |

## Einheiten und Kampf

| Nr. | Anforderung | Seit | Stand |
|---|---|---|---|
| K1 | **Jede Einheit einzeln kennen**: Stärken, Schwächen, Werte, Fähigkeiten – nicht nach Stufe urteilen. Beispiel: Vampirfürsten sind viel gefährlicher als Basilisken. | 16.11. | teilweise (Schaden aus der Spielformel; ein Teil der Fähigkeiten im Simulator, siehe K3) |
| K2 | Das gilt auch für den **Einsatz der eigenen Einheiten**. Beispiel: Champions machen mehr Schaden, je weiter sie vor dem Angriff laufen. | 16.11. | teilweise (Anlauf, Zurückfliegen, Lebensraub, Feuerschild u. a. nachgebildet) |
| K3 | **Harte Konter berücksichtigen.** Beispiel: Mächtige Gorgonen – Todesblick einrechnen, nicht mit wertvollen Stufe-7-Einheiten in den Nahkampf gehen. | 17.11. | offen (Todesblick, Versteinern, Lähmen, Blenden, Altern, Gift, Fluch, Säureatem, Furcht, zaubernde Einheiten fehlen im Simulator) |
| K4 | **Vor jedem Kampf auf der Karte prüfen, ob er sich lohnt** – mit allem, was der Gegner kann. | 17.11. | teilweise (simulierte Prognose für Banken im Test, für Kartenwachen als Schalter; ohne die Fähigkeiten aus K3) |
| K5 | **Immunitäten gegen die eigenen Zauber.** Beispiel: Ein Held, der nur mit Implosion gewinnen kann, hat gegen Magmaelementare (immun gegen Erdzauber) keine Chance. | 17.11. | offen (die Prognose rechnet eigene Zauber bisher gar nicht mit) |
| K6 | **Tempo:** Wer hat den ersten Zug? Ziehen alle eigenen Einheiten rechtzeitig, damit der Plan aufgeht? | 17.11. | teilweise (Zugreihenfolge im Simulator; kein Plan über mehrere Einheiten) |
| K7 | **Taktik:** nutzen – und einrechnen, dass der Gegner sie auch haben kann und dann schneller ist. | 17.11. | offen (Aufstellungsphase wird sofort beendet) |
| K8 | Wirksamkeit der eigenen Armee **gegen die konkrete gegnerische Armee** beurteilen, nicht nur Zahlen vergleichen. | 17.11. | teilweise (Heldenkämpfe nach Armeewert mal Heldenwert; Simulation als Schalter) |
| K9 | Belagerungen selbst spielen. | 16.11. | offen |

## Armeezusammenstellung

| Nr. | Anforderung | Seit | Stand |
|---|---|---|---|
| A1 | Truppen müssen **zur Armee passen** (Anlass: ein Tower-Held kaufte 12 Skelettkrieger im Flüchtlingslager). | 17.11. | im Test (Schalter `armyFit`) |
| A2 | Dabei zählt **nicht nur Moral, sondern auch die Stärke der Kreaturen**. Beispiel: Geisterdrachen an Tag 1 wären sehr wahrscheinlich ein guter Kauf, trotz Moralverlust. | 17.11. | im Test (`armyFit` rechnet Wert der Truppe gegen Moralverlust der übrigen Armee) |
| A3 | Viele Variablen abwägen: **Einheitenstärke, Moral, Synergien, Kontermöglichkeiten** (z. B. Mächtige Gorgonen gegen Stufe-7-Kreaturen), **Artefakt-Synergien, Synergien mit Sekundärfähigkeiten, Wirksamkeit gegen die gegnerische Armee** usw. | 17.11. | offen (geplant: Armeen über simulierte Kämpfe bewerten) |

## Helden

| Nr. | Anforderung | Seit | Stand |
|---|---|---|---|
| H1 | **Sekundärfähigkeiten** so klug wie möglich wählen – für Haupt- und Nebenhelden unterschiedlich. | 16.11. | im Test (Schalter `skillChoice`; Standard ist noch eine feste Rangliste) |
| H2 | **Artefakte:** bei mehreren für denselben Platz das beste tragen. | 16.11. | im Test (Schalter `artifactCare`) |
| H3 | Artefakte auf der Karte **nach ihrem Wert für den eigenen Helden** einstufen. Beispiel: Kugel der Hemmung – für einen Krieger gut, für einen Magier schädlich. | 16.11. | im Test (`artifactCare`) |
| H4 | Heldenwahl (Taverne) nach Rolle, Truppen, Spezialität. | 16.11. | im Test (Schalter `heroChoice`) |

## Magie und Mana

| Nr. | Anforderung | Seit | Stand |
|---|---|---|---|
| M1 | **Kampf: nur ein Zauber pro Runde – die Wahl ist entscheidend.** Jeden möglichen Zauber und „kein Zauber" in derselben Vorausberechnung wie die Einheitenzüge bewerten, samt Zeitpunkt. | 16.11. | teilweise (Schalter `spellSearch`; Standard ist noch VCMIs Zauberlogik) |
| M2 | Dabei berücksichtigen: eigenes und **gegnerisches Restmana**; Werte beider Helden (Zauberkraft, Wissen, Schulstufen, Massenzauber, Artefakte); Resistenzen und Immunitäten; schon wirkende Zauber und ihre Restdauer; **bisher vom Gegner gewirkte Zauber** (merken). Vom Gegner nur, was ein Mensch im Kampf sieht. | 16.11. | teilweise (gemerkte Zauber fließen ins Heldenmodell; Rest offen) |
| M3 | Besser zaubern als die Standard-KI, die manche Zauber gar nicht oder schlecht nutzt. | 16.11. | offen |
| M4 | **Mana-Management.** Ein Magierheld mit wenig oder ohne Mana ist bedeutend schwächer – das muss in jede Kampfentscheidung eingehen. Vor einem Angriff auf den Gegner prüfen, ob das Mana reicht. | 17.11. | offen |
| M5 | **Zauber nicht verschwenden.** Immer abwägen, ob ein Zauber sinnvoll und wichtig ist. Keine Schadenszauber auf schwache Helden, die man ohnehin mühelos schlägt; keine Zauber in neutralen Kämpfen, die ohne Verluste zu gewinnen sind. | 17.11. | offen (die Standard-Zauberlogik zaubert, sobald es irgendetwas bringt) |
| M6 | **Abenteuerzauber selbst nutzen:** Dimensionstor, Fliegen, Wasserwandeln, Stadtportal, Boot rufen und versenken, Visionen, Luft- und Erdsicht, Täuschung. | 16.11. | teilweise (Fliegen und Wasserwandeln; der Rest offen) |
| M7 | Bei **feindlichen Helden einplanen**, dass sie solche Zauber haben können. | 16.11. | offen |
| M8 | **Täuschung (Disguise) erkennen:** Sie verfälscht die angezeigte Armee. Ein Mensch merkt das meist; FairAI soll solche Armeen als verdächtig einstufen. | 16.11. | offen |

## Wirtschaft

| Nr. | Anforderung | Seit | Stand |
|---|---|---|---|
| W1 | **Ressourcen vorausschauend:** Minen der Ressource bevorzugen, die die eigene Fraktion brauchen wird. Beispiele: Edelsteine für Castle, Kristalle für Rampart. | 16.11. | im Test (Schalter `resourcePlan`; einfacher Bedarfsfaktor ist Standard) |

## Gebäude und Orte auf der Karte

| Nr. | Anforderung | Seit | Stand |
|---|---|---|---|
| G1 | **Kein Gebäude wird pauschal ignoriert.** Jedes wird nach seinem Nutzen in der aktuellen Lage bewertet. | 17.11. | im Test (Schalter `tables` und `sites`) |
| G2 | Beispiele aus dem Spiel der Profis: früh Wyvern aus dem Dragon Fly Hive, Engel aus dem Griffin Conservatory, Naga-Banken und ähnliche Gebäude früh räumen. | 17.11. | im Test (`tables` = 2: Kreaturenbanken mit simuliertem Kampf) |
| G3 | **In Betracht ziehen heißt nicht besuchen.** Beispiel Sanctuary: Ist weit und breit kein Gegner, wäre der Weg dorthin verschwendet. Flieht man aber vor einem Helden, der einen in der nächsten Runde einholen würde, ist es auf einmal wertvoll – und man kann dort bleiben. | 17.11. | erfüllt (17.11.: Zuflucht nur bei Bedrohung, sonst Wert null) – Prüfung in einer Partie steht aus |
| G4 | Beispiel **Schwanenteich:** Der Glücksbonus steht in keinem Verhältnis dazu, alle Bewegungspunkte des Tages zu verlieren. Kann man aber nur noch 1–2 Felder weit reiten, ist ein Teich in Reichweite und sonst nichts Wichtiges, kann man es andenken. | 17.11. | erfüllt (17.11.: Der verlorene Rest des Tages zählt als Reisezeit; Glück nach Größe der Armee) |

## Gegner einschätzen

| Nr. | Anforderung | Seit | Stand |
|---|---|---|---|
| E1 | **Laufwege und Reichweite gegnerischer Helden berechnen:** Wer kann mich oder meine Städte wann erreichen, wen kann ich abfangen? | 16.11. | teilweise (Bedrohung über begehbares Land; ohne Stadtportal, Portale, ungesehene Helden) |
| E2 | Gegnerische Helden nach allem einschätzen, was ein Mensch sehen kann (Kämpfe, Diebesgilde, Klasse, Spezialität, gezeigte Zauber). | 16.11. | erfüllt (Standard seit 17.11.) |

## Strategie

| Nr. | Anforderung | Seit | Stand |
|---|---|---|---|
| S1 | **Strategien erfahrener Spieler** kennen und anwenden (Unterlage `homm3-profi-strategien.md` im Projekt). | 16.11. | teilweise |
| S2 | Früh ausbrechen und ausbreiten wie ein guter Spieler (Grenzwachen, zweite und dritte Stadt). | 17.11. (eigener Befund) | im Test (Schalter `passage` mit `regionValue`) |

---

## Wörtliche Anmerkungen des Nutzers vom 17.11.2026

Damit beim Umsetzen nichts verloren geht, hier die Kernsätze im Wortlaut:

> „generell sollte die KI jedes gebäude auf der karte besuchen, wenn es sinnvoll ist. keine gebäude
> sollten generell ignoriert werden. es ist alles situativ"

> „bei der armeezusammenstellung ist nicht nur moral, sondern auch stärke der kreaturen wichtig. wenn
> man an tag 1 z.B. geisterdrachen kaufen hätte können, wäre das sehr wahrscheinlich ein guter kauf,
> auch mit moralverlust."

> „und nur weil man alle gebäude und optionen in betracht ziehen soll, muss man nicht alles machen und
> alle gebäude fix besuchen. wenn z.B. ein sanctuary entdeckt wird, aber weit und breit kein gegner
> ist, wäre es eine verschwendung, dorthin zu reiten. wenn man hingegen gerade vor einem helden
> flieht, der einen in der nächsten runde einholen würde, ist das gebäude auf einmal wertvoll und man
> kann dort bleiben."

> „bei schwanenteichen ist es ähnlich. der glücksboost den man bekommt steht in keinem verhältnis zum
> nachteil, dass man sämtliche bewegungspunkte für den restlichen tag verliert. wenn man aber zufällig
> nur noch 1-2 felder weit reiten könnte, und ein schwanensee in reichweite ist, und sonst nichts
> wichtiges, kann man es andenken."

> „die armeezusammenstellung hat auch enorm viele variablen. unitstärke, moral, synergien,
> kontermöglichkeiten (z.B. mighty gorgons gegen stufe 7 kreaturen), artefaktsynergien, secondary
> skills synergien, wirksamkeit gegen gegnerische armee, usw."

> „auch beim angreifen von units muss man eventuelle hardcounter berücksichtigen. wenn man z.B. mighty
> gorgons angreift, muss man deren death stare ability miteinkalkulieren und sollte nicht mit
> wertvollen tier 7 units in den nahkampf gehen. dann muss man abwägen, ob der kampf sich überhaupt
> lohnt bevor man ihn auf der weltkarte eingeht. genauso wenn man gegen kreaturen mit magieimmunitäten
> kämpft und die eigene armee nur durch starke zauber des helden gewinnen würde - wenn ein held nur
> implosion als möglichen zauber hat, und nur damit gewinnen kann, wird er gegen magmaelementare keine
> chance haben, da diese gegen erdzauber immun sind. auch das muss vor jedem kampf evaluiert werden.
> genauso die geschwindigkeit: wer wird in dem kampf den ersten zug haben? ziehen alle einheiten
> rechtzeitig, damit der eigene plan aufgeht? wenn ich mich auf tactics verlasse, hat der gegnerische
> held eventuell auch tactics und ist dann schneller als ich? usw."

> „du musst vorher wirklich jeden aspekt des spiels genau untersuchen und der KI klar machen, welche
> nutzen und gefahren jede entscheidung mit sich bringt."

> „denke auch an mana-management. wenn man kämpfe als magierheld ohne mana oder mit wenig mana
> bestreitet, muss klar sein, dass man damit bedeutend schwächer ist. genauso wenn man den gegner
> angreift - vorher überprüfen, ob man genug mana hat. bei spells im kampf immer abwägen, ob es
> sinnvoll und wichtig ist, sie zu casten. z.B. wenn gegnerische schwache helden immer wieder
> angreifen und man sinnlos damagespells auf sie zaubert, obwohl man auch so mühelos gewonnen hätte,
> wäre das eine verschwendung. oder wenn man in neutralen kämpfen, die mühelos ohne verluste gewonnen
> werden können, zauber verwendet"

> „schreibe dir alles, was ich an wichtigen punkten festhalte irgendwo auf github auf, damit du es
> nicht vergisst und alle punkte bei der fertigen KI dann berücksichtigt werden. auch die sachen, die
> ich in den vergangenen tagen immer wieder angemerkt habe"

Die Punkte vom 15. und 16.11. sind oben sinngemäß eingetragen (Quelle: Gesprächsnotizen und Roadmap
des Projekts). Fehlt etwas oder ist etwas falsch wiedergegeben, wird es hier nachgetragen.
