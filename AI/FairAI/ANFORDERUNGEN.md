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

Letzte Änderung: 18.11.2026

---

## Leitsätze (gelten für alles Weitere)

| Nr. | Anforderung | Seit | Stand |
|---|---|---|---|
| L1 | **Nicht schummeln.** Kein Maphack, keine Extraboni; nur Wissen, das ein Mensch auf demselben Platz hätte. | 15.11. | erfüllt (Prüfung `arena/check_fairness.py` vor jedem Stand) |
| L2 | **So stark und klug wie möglich** – auf allen gewöhnlichen Karten, nicht nur auf einer. | 15.11./16.11. | offen (siehe Zielmarke Z1) |
| L3 | **Alles ist situativ.** Nichts wird pauschal getan oder pauschal gelassen. Jede Option wird in Betracht gezogen, aber nur ausgeführt, wenn sie in dieser Lage etwas bringt. „Nur weil man alle Gebäude und Optionen in Betracht ziehen soll, muss man nicht alles machen." | 17.11. | teilweise |
| L4 | **Jeden Aspekt des Spiels vorher genau untersuchen** und der KI klarmachen, welchen Nutzen und welche Gefahren jede Entscheidung mit sich bringt. | 17.11. | teilweise (Einheiten: `ability-catalog.md` listet alle 90 Arten von Fähigkeiten der 199 Kreaturen des installierten Spiels samt HotA, erzeugt aus dem laufenden Spiel, mit Stand je Fähigkeit – 33 im Simulator, 17 hinter einem Schalter, 22 offen. Gebäude: Bewertung nach der Tabelle jeder Gebäudeart. Offen: dasselbe für Zauber, Artefakte, Sekundärfähigkeiten, Stadtgebäude) |
| L5 | **Nach jedem Schritt neu bewerten**, wenn neue Informationen auftauchen. | 15.11. | erfüllt |
| L6 | **Nebel, der neu entsteht** (um Nekropolen, Cover of Darkness), verstehen und klug darauf reagieren. | 15.11. | teilweise (wieder verdecktes Land gilt als veraltet; auf Feldzug wie unerkundet) |
| L7 | Eine KI für Abenteuerkarte **und** Kampf. | 15.11. | erfüllt (Belagerungen und Teile der Zauberwahl spielt noch VCMIs Kampflogik, siehe K9, M1) |

## Ziel und Umfang

| Nr. | Anforderung | Seit | Stand |
|---|---|---|---|
| Z1 | **Zielmarke:** mindestens 95 % aller Partien gegen die Standard-KI gewinnen, möglichst auch 95 % gegen Nullkiller2 mit der Kampf-KI MMAI. | 17.11. | offen (mittlere Landkarten, beste Einstellungen: 56 % Siege, 16 % Niederlagen, 28 % offen nach 150 Tagen) |
| Z2 | Alle gewöhnlichen Karten mit dem Ziel „alle Gegner besiegen": winzig bis riesig, Land und Inseln, auch brandneue Zufallskarten. Keine Spezialkarten mit Sonder-Siegbedingung (z. B. Lex) – die kommen danach. | 16.11. | teilweise (läuft überall; gemessen und gut nur auf kleinen und mittleren Landkarten) |
| Z3 | 1 gegen 1, jeder gegen jeden, mehrere Gegner, Teams. **Verbündete korrekt erkennen.** | 16.11. | teilweise (Verbündete sind keine Ziele; Zusammenspiel im Team fehlt) |
| Z4 | **Alle Fraktionen einschließlich HotA**: Cove, Factory, Bulwark. | 16.11. | teilweise (alle zwölf spielen ohne Absturz; Stärke je Fraktion sehr verschieden) |
| Z5 | Die Entwicklung soll nicht Monate dauern. | 15.11. | – |

## Einheiten und Kampf

| Nr. | Anforderung | Seit | Stand |
|---|---|---|---|
| K1 | **Jede Einheit einzeln kennen**: Stärken, Schwächen, Werte, Fähigkeiten – nicht nach Stufe urteilen. Beispiel: Vampirfürsten sind viel gefährlicher als Basilisken. | 16.11. | teilweise (Schaden aus der Spielformel; ein Teil der Fähigkeiten im Simulator, siehe K3) |
| K2 | Das gilt auch für den **Einsatz der eigenen Einheiten**. Beispiel: Champions machen mehr Schaden, je weiter sie vor dem Angriff laufen. | 16.11. | teilweise (Anlauf, Zurückfliegen, Lebensraub, Feuerschild u. a. nachgebildet) |
| K3 | **Harte Konter berücksichtigen.** Beispiel: Mächtige Gorgonen – Todesblick einrechnen, nicht mit wertvollen Stufe-7-Einheiten in den Nahkampf gehen. | 17.11. | im Test (Schalter `simAfterHit`: Todesblick, Versteinern, Lähmen, Blenden, Altern, Gift, Fluch, Schwäche, Säure, Donnerschlag sind im Simulator und im Einheiten-Labor gegen das Spiel bestätigt – Mächtige Gorgonen gegen Erzengel: vorher 4,3-mal so viel Schaden wie vorhergesagt, jetzt 1,15-mal. Offen: Furcht, zaubernde Einheiten, und dass die Kampf-KI danach handelt, ist noch nicht eigens gemessen) |
| K4 | **Vor jedem Kampf auf der Karte prüfen, ob er sich lohnt** – mit allem, was der Gegner kann. | 17.11. | teilweise (simulierte Prognose für Banken ist Standard, für Kartenwachen als Schalter `fightForecast` im Test; mit den Fähigkeiten aus K3 hinter `simAfterHit`, mit den eigenen Schadenszaubern hinter `spellForecast`) |
| K5 | **Immunitäten gegen die eigenen Zauber.** Beispiel: Ein Held, der nur mit Implosion gewinnen kann, hat gegen Magmaelementare (immun gegen Erdzauber) keine Chance. | 17.11. | im Test (Schalter `spellForecast`: Die Kampfprognose spielt die Schadenszauber des eigenen Helden mit – ein Zauber pro Runde, solange das Mana reicht – und rechnet für jeden gegnerischen Trupp aus, was der Zauber dort anrichtet: nichts bei Immunität gegen den Zauber, seine Stufe oder seine Schule, weniger bei Schutz (Golems), mehr bei Schwäche (Elementare), dazu die Chance zu widerstehen. Die Regeln stammen aus den Daten des Spiels und wurden in echten Kämpfen mit dem Spiel verglichen: 617.000 Paare aus Zauber und Kreatur, alle 193 Kreaturen samt HotA, keine Abweichung außer bei Hypnose, die kein Schadenszauber ist. Hinweis zum Beispiel: Im Spiel sind Erd- und Magmaelementare immun gegen Blitz, Kettenblitz und Armageddon, nicht gegen Erdzauber – Implosion wirkt auf sie. FairAI richtet sich nach den Daten des Spiels. Offen: Zauber, die keinen Schaden machen – Verlangsamen, Hast, Blenden, Auferstehung u. a.) |
| K6 | **Tempo:** Wer hat den ersten Zug? Ziehen alle eigenen Einheiten rechtzeitig, damit der Plan aufgeht? | 17.11. | teilweise (Zugreihenfolge im Simulator; kein Plan über mehrere Einheiten) |
| K7 | **Taktik:** nutzen – und einrechnen, dass der Gegner sie auch haben kann und dann schneller ist. | 17.11. | offen (Aufstellungsphase wird sofort beendet) |
| K8 | Wirksamkeit der eigenen Armee **gegen die konkrete gegnerische Armee** beurteilen, nicht nur Zahlen vergleichen. | 17.11. | teilweise (Heldenkämpfe nach Armeewert mal Heldenwert; Simulation als Schalter) |
| K9 | Belagerungen selbst spielen. | 16.11. | offen (17.11. gemessen: Belagerungen kosten die Hauptarmee 88.000 pro Partie; rund 70 % der Verluste machen die Türme, und Läuferarmeen brauchen 7 bis 13 Runden statt 2 bis 3 mit vielen Fliegern, weil sie auf das Katapult warten) |
| K10 | **Ausnahmen der Fähigkeiten kennen.** Beispiel: Der Lebensraub der Vampirfürsten wirkt nicht gegen Elementare und andere nicht-lebende Kreaturen. | 17.11. | teilweise (im Kampfsimulator erfüllt: Lebensraub nur aus Schaden an Lebenden nach der Regel des Spiels – keine Untoten, Elementare, Golems, Gargoyles, Maschinen. Die Gargoyles fehlten bis 17.11. in dieser Ausnahme und sind nachgetragen. Im Einheiten-Labor geprüft: Vampirfürsten heilen gegen Oger wie vorhergesagt, gegen Erdelementare und Obsidian-Gargoyles gar nicht. Die Entscheidung vor dem Kampf nutzt den Simulator für Kartenwachen noch nicht im Standard, siehe K4) |
| K11 | **Kämpfe gegen neutrale Wachen verlustfrei gewinnen, wenn es einen Weg gibt:** vor dem Kampf durchrechnen, wie er mit Schützen, vielen 1er-Stacks und defensivem Spiel ohne Verluste zu gewinnen ist – oder so, dass nur ein, zwei 1er-Stacks fallen. | 18.11. | teilweise (in der ersten Partie des Nutzers verlor FairAI in vier Wachenkämpfen 5.866 von 12.609 Armeewert, der Nutzer in 13 Kämpfen fast nichts. **Seit 20.11. Standard: das Kampf-Paket** – jede Aktion wird mit vier Plänen für die ganze Armee mehrere Runden vorausgespielt (halten, vorrücken, lauern, um den Schützen sammeln), die Wachen dabei nach einem Nachbau ihrer eigenen Kampf-KI (trifft 99 % ihrer Züge), eigene Zauber-Einheiten spielt FairAI selbst, ein gewonnener Kampf zählt Kreaturen statt Lebenspunkte. Gemessen: 579 gestellte Wachenkämpfe 17,3 % statt 21,2 % Verlust; 300 Kämpfe aus mitgeschriebenen Partien 4,5 % statt 7,1 %, davon 160 statt 75 ganz ohne Verlust; in ganzen Partien bis Tag 42 rund 8.000 weniger Verlust an Wachen und 15.000 mehr Armee. Als Schalter gebaut, noch aus: „schießen und außer Reichweite bleiben“ (`kitePlan`), „warten, dann vorrücken“ (`latePlan`), Wachenkämpfe nach simulierter Prognose annehmen (`fightForecast` 2). Offen: schießende Wachen (dort hilft das Paket nicht: 7,8 % gegen 8,0 %), Abschirmen, Blocken, die Zauber des Helden nach Plan; Einzelkreaturen als Köder (`baitStacks`) brachten gegen VCMIs Kampf-KI nichts) |
| K12 | **1er-Stacks des Gegners erkennen:** nicht mit einem wertvollen Trupp einen einzelnen Zentauren angreifen, während die Schützen dahinter weiterschießen. | 18.11. (eigener Befund aus der Partie des Nutzers) | offen (Tag 25: 20.321 gegen 10.135 gewonnen, aber mit 43 % Verlust; 15 Großelfen schossen vier Salven, bevor unsere Truppen zuschlugen) |
| K13 | **Gestellte Testkämpfe, beide Seiten bestmöglich gespielt** (Karte TESTBERGER des Nutzers, fünf Kämpfe): gleiche Umgebung wie im Spiel, Helden zählen nicht, Stacks werden nicht umverteilt; jede Seite soll mit möglichst wenig Verlust möglichst viel Schaden anrichten. Bild 1 (Großelfen, Zentauren, Pegasus, Zwerge gegen 150 Gnoll-Marodeure): die Gnolle dürfen nicht ewig warten. Bild 2 (10 Vampirlords gegen 20 Dendroidenwächter) und Bild 3 (gegen 5 Hydren): verlustfrei gewinnen, „egal, wer steuert“. Bild 4 (10 Mächtige Gorgonen + 6 Gnoll-Stacks gegen 3 Uralte Behemoths) und Bild 5 (20 Wolfsreiter + 6 Goblin-Stacks gegen 3 Todesritter): beste Spielweise beider Seiten. Dazu die Karte „With Sword and Fire“ (jeder Kampf muss verlustfrei gewonnen werden). | 23.11. | teilweise. **Werkzeug:** Kämpfe lassen sich von außen spielen – beide Seiten nach geschriebenen Strategien, das Spiel als Schiedsrichter (`arena/puppet.py`, `AI/FairAI/Combat/Puppet.h`), auf demselben Feld wie im Spiel (gleiche Kachel, gleiche Hindernisse), genau wie im Spiel, ohne Moral- und Glückswürfel oder mit festem Startwert; `arena/pursuit.py` löst das Jagdspiel „schneller Flieger gegen einen Läufer“ vollständig. **Bild 2 gelöst:** warten, am Rundenende zuschlagen, zu Beginn der nächsten Runde außer Reichweite fliegen – 24.141 von 24.806 Stellungen sind für die Vampirlords gewonnen, die Startstellung auch; im Spiel 300 von 300 Kämpfen ohne Verlust gegen sechs Gegner (FairAI heute: verliert alle zehn Vampirlords). Die Grenze liegt bei Tempo 4 des Läufers: gegen Tempo 5 kann der Läufer das freie Zuschlagen auf diesem Feld immer verhindern (0 von 25.440 Stellungen). **Bild 3 offen:** deshalb gibt es dort keinen sicheren Weg; kein Versuch gewann (0 von 240). **Bild 1:** bestes Spiel kostet links zwei einzelne Zentauren (200 von 10.083 Armeewert); behoben: Truppen ohne Fernwaffe, die beschossen werden, rücken jetzt vor (`outranged`), statt bis zum letzten Mann zu warten. **Bild 5:** die Todesritter gewinnen ohne Verlust, wenn sie nur am Rundenende zuschlagen und nie in Reichweite der Wolfsreiter stehen, während der Gegner am Zug ist (49 von 50); gegen die heutigen Kampf-KIs kostet eine Eckfestung mit doppelter Mauer sie in 92 % der Kämpfe einen Ritter. **Bild 4:** mit derselben Spielweise gewinnen die Behemoths etwa drei von vier Kämpfen; spielen sie wie die heutigen Kampf-KIs, gewinnt links 98–100 %. Offen: diese Spielweisen in FairAIs eigene Kampf-KI übernehmen (heute stehen sie nur im Prüfstand); „With Sword and Fire“ ist eingelesen, noch nicht gespielt |

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
| M1 | **Kampf: nur ein Zauber pro Runde – die Wahl ist entscheidend.** Jeden möglichen Zauber und „kein Zauber" in derselben Vorausberechnung wie die Einheitenzüge bewerten, samt Zeitpunkt. | 16.11. | im Test (Schalter `spellSearch` 2: jeder mögliche Zauber und „kein Zauber" werden bis zum Ende des Kampfes durchgerechnet; Standard ist noch VCMIs Zauberlogik) |
| M2 | Dabei berücksichtigen: eigenes und **gegnerisches Restmana**; Werte beider Helden (Zauberkraft, Wissen, Schulstufen, Massenzauber, Artefakte); Resistenzen und Immunitäten; schon wirkende Zauber und ihre Restdauer; **bisher vom Gegner gewirkte Zauber** (merken). Vom Gegner nur, was ein Mensch im Kampf sieht. | 16.11. | teilweise (eigenes Mana, Zauberkraft, Schulstufen, Artefakte, Spezialität, Immunitäten und Resistenzen gehen in die Prognose ein – Schalter `spellForecast`; vom Gegner die Schadenszauber, die wir ihn wirken sahen, mit geschätzter Zauberkraft. Offen: gegnerisches Restmana, wirkende Zauber und ihre Dauer, Zauber ohne Schaden) |
| M3 | Besser zaubern als die Standard-KI, die manche Zauber gar nicht oder schlecht nutzt. | 16.11. | offen |
| M4 | **Mana-Management.** Ein Magierheld mit wenig oder ohne Mana ist bedeutend schwächer – das muss in jede Kampfentscheidung eingehen. Vor einem Angriff auf den Gegner prüfen, ob das Mana reicht. | 17.11. | im Test (`spellForecast`: Die Prognose rechnet mit dem Mana, das der Held jetzt hat. Ohne Mana zählt er als das, was er dann ist – in 372 Wachenkämpfen von Magierhelden lag die Prognose ohne Zauber um 19 Prozentpunkte zu hoch, mit Zaubern um 3. Die Zauberpunkte, die ein Kampf kostet, zählen zu seinem Preis. Im Kampf: `manaUrgency`, siehe M5. Offen: Mana gezielt auffüllen – Brunnen, Stadt mit Gilde – als eigener Wert) |
| M5 | **Zauber nicht verschwenden.** Immer abwägen, ob ein Zauber sinnvoll und wichtig ist. Keine Schadenszauber auf schwache Helden, die man ohnehin mühelos schlägt; keine Zauber in neutralen Kämpfen, die ohne Verluste zu gewinnen sind. | 17.11. | im Test (`spellSearch` 2: Ein Zauber wird nur gewirkt, wenn er den Ausgang ändert – weniger eigene Verluste oder Sieg statt Niederlage – und das mehr wert ist als die Zauberpunkte. In 80 Heldenkämpfen nicht schwächer als die alte Logik: 74 % statt 70 % Siege. Partientest steht aus) |
| M6 | **Abenteuerzauber selbst nutzen:** Dimensionstor, Fliegen, Wasserwandeln, Stadtportal, Boot rufen und versenken, Visionen, Luft- und Erdsicht, Täuschung. | 16.11. | teilweise (Fliegen und Wasserwandeln; der Rest offen) |
| M7 | Bei **feindlichen Helden einplanen**, dass sie solche Zauber haben können. | 16.11. | offen |
| M8 | **Täuschung (Disguise) erkennen:** Sie verfälscht die angezeigte Armee. Ein Mensch merkt das meist; FairAI soll solche Armeen als verdächtig einstufen. | 16.11. | offen |
| M9 | **Alle Zauber des Spiels benutzen können** – im Kampf auch die, die einen Plan brauchen (Teleport, Kraftfeld, Treibsand, Klonen, Opfer, Berserker, Blenden und stehen lassen, Wiederbelebung am Kampfende), auf der Karte alle Abenteuerzauber. | 18.11. | teilweise (Kampf: Die Zauberwahl rechnet jeden Zauber durch, das trägt bei Schadenszaubern und einfachen Segen und Flüchen; Karte: nur Fliegen und Wasserwandeln, siehe M6) |

## Wirtschaft

| Nr. | Anforderung | Seit | Stand |
|---|---|---|---|
| W1 | **Ressourcen vorausschauend:** Minen der Ressource bevorzugen, die die eigene Fraktion brauchen wird. Beispiele: Edelsteine für Castle, Kristalle für Rampart. | 16.11. | im Test (Schalter `resourcePlan`; einfacher Bedarfsfaktor ist Standard) |
| W2 | **Kapitol früh bauen, dafür sparen.** In der ersten Partie des Nutzers war die Burg (Voraussetzung fürs Kapitol) von Tag 9 bis Tag 36 das beste Gebäude und wurde nie gebaut, weil jeden Tag das nächstbeste bezahlbare Gebäude oder Truppen das Gold nahmen: 2.000 Gold am Tag gegen 4.000 des Nutzers ab Tag 14. | 18.11. (Befund aus der Partie des Nutzers) | **Standard seit 20.11.** (`saveFor`: Für eine Halle oder was eine Halle braucht wird der Preis zurückgelegt; überschüssige Rohstoffe werden dafür verkauft, wenn das Gebäude damit heute bezahlt ist. In 16 Probepartien steht das Kapitol an Tag 12 bis 19 statt an Tag 13 bis 36. Gegen Nullkiller2 gewinnt oder verliert die Regel keine Partien: −0,07 ± 0,09 in 383 und +0,02 ± 0,08 in 347 Partien. Was sie dort bewirkt: an Tag 14 Einkommen +700 am Tag und Armee −2.700 von 13.800, an Tag 28 Einkommen +1.600 von 5.600 und die Armee wieder gleichauf) |
| W3 | **Wer den Gegner kommen sieht, rekrutiert.** „Obwohl sie mich kommen sah, hat sie wenige Einheiten in ihrer Burg rekrutiert … in ihrer Burg konnten sehr viele rekrutiert werden, als ich sie einnahm.“ Ungekaufte Kreaturen gehören dem Eroberer. | 18.11. | teilweise (immer an: Steht ein Gegnerheld sichtbar vor der Stadt, wird alles gekauft, was das Gold hergibt. Die Ursache in der Partie war W2: Kreaturen für 24.425 Gold lagen bei 20 Gold in der Kasse. Die weitergehende Regel `threatSpend` 1 – schon bei einem überlegenen Helden in zwei Tagesmärschen nur noch Mauern bauen und Rohstoffe für Truppen verkaufen – **kostet gegen Nullkiller2 Partien** (−0,10 ± 0,07 in 347 Partien, 43 statt 28 Niederlagen): dort ist wochenlang ein stärkerer Held in der Nähe, die Stadt hört auf zu bauen, und die gekauften Truppen fallen mit ihr. Neu am 20.11., noch aus und im Test: Stufe 2 – nur dort, wo es etwas entscheidet: wenn die Stadt mit den wartenden Truppen auf 70 % dessen kommt, was anrückt; Rohstoffe werden nur verkauft, wenn das Gold nicht reicht) |

## Gebäude und Orte auf der Karte

| Nr. | Anforderung | Seit | Stand |
|---|---|---|---|
| G1 | **Kein Gebäude wird pauschal ignoriert.** Jedes wird nach seinem Nutzen in der aktuellen Lage bewertet. | 17.11. | im Test (Schalter `tables` und `sites`) |
| G2 | Beispiele aus dem Spiel der Profis: früh Wyvern aus dem Dragon Fly Hive, Engel aus dem Griffin Conservatory, Naga-Banken und ähnliche Gebäude früh räumen. | 17.11. | im Test (`tables` = 2: Kreaturenbanken mit simuliertem Kampf) |
| G3 | **In Betracht ziehen heißt nicht besuchen.** Beispiel Sanctuary: Ist weit und breit kein Gegner, wäre der Weg dorthin verschwendet. Flieht man aber vor einem Helden, der einen in der nächsten Runde einholen würde, ist es auf einmal wertvoll – und man kann dort bleiben. | 17.11. | erfüllt (17.11.: Zuflucht nur bei Bedrohung, sonst Wert null; in einer gestellten Lage geprüft: Verfolger mit fünffacher Stärke, keine eigene Stadt in Tagesreichweite – der Held geht ins Sanctuary und beendet dort den Tag) |
| G4 | Beispiel **Schwanenteich:** Der Glücksbonus steht in keinem Verhältnis dazu, alle Bewegungspunkte des Tages zu verlieren. Kann man aber nur noch 1–2 Felder weit reiten, ist ein Teich in Reichweite und sonst nichts Wichtiges, kann man es andenken. | 17.11. | erfüllt (17.11.: Der verlorene Rest des Tages zählt als Reisezeit; Glück nach Größe der Armee) |

## Gegner einschätzen

| Nr. | Anforderung | Seit | Stand |
|---|---|---|---|
| E1 | **Laufwege und Reichweite gegnerischer Helden berechnen:** Wer kann mich oder meine Städte wann erreichen, wen kann ich abfangen? | 16.11. | teilweise (Bedrohung über begehbares Land; ohne Stadtportal, Portale, ungesehene Helden) |
| E2 | Gegnerische Helden nach allem einschätzen, was ein Mensch sehen kann (Kämpfe, Diebesgilde, Klasse, Spezialität, gezeigte Zauber). | 16.11. | teilweise (Stärke aus Kämpfen, Diebesgilde, Klasse, Spezialität und gezeigten Zaubern ist Standard. 17.11.: Der Wert gezeigter Zauber war geschätzt und viel zu hoch – Massen-Verlangsamen zählte 1,6-fach, gemessen sind 1,05. Die gemessenen Werte aus 1.344 gestellten Kämpfen sind im Test: Schalter `magicWorth`) |
| E3 | **Was das Kampffenster vom Gegnerhelden zeigt, merken** (Angriff, Verteidigung, Zauberkraft, Wissen, Mana – wie es jeder Spieler sieht) und **neue Helden nicht für alte halten**: wer lange nach dem ersten Kontakt zum ersten Mal auftaucht, ist frisch angeheuert, auch mit großer Armee. | 17.11. (eigener Befund) | Merken: behoben (wurde nie gespeichert; seitdem Schätzung Angriff 7,7 / Verteidigung 9,8 gegen gezeigt 7,8 / 10,0). Neue Helden: im Test (Schalter `freshHeroes`; bisher Angriff 8,7 angenommen, gezeigt 1,6) |

## Strategie

| Nr. | Anforderung | Seit | Stand |
|---|---|---|---|
| S1 | **Strategien erfahrener Spieler** kennen und anwenden (Unterlage `homm3-profi-strategien.md` im Projekt). | 16.11. | teilweise |
| S2 | Früh ausbrechen und ausbreiten wie ein guter Spieler (Grenzwachen, zweite und dritte Stadt). | 17.11. (eigener Befund) | im Test (Schalter `passage` mit `regionValue`) |
| S3 | **Keine Armeen verschenken.** Wer angreift, gewinnt: FairAI gewinnt jeden Kampf, den es beginnt, und verliert 98 % derer, in die es gezogen wird. Nebenhelden mit echter Armee dürfen nicht im Freien stehen; Garnisonen, die nicht halten können, sind Futter. | 17.11. (eigener Befund) | im Test (Schalter `safeParts`: Nebenhelden mit mehr als 15.000 Truppen beenden jeden Tag in einer eigenen Stadt oder beim Haupthelden; Kurier startet nur, wenn er am selben Tag ankommt. Garnisonen ohne Held: offen) |
| S4 | **Überlegenheit in einen Sieg verwandeln.** In offenen Partien ist die eigene Hauptarmee an zwei von drei Tagen mindestens 1,5-mal so groß wie die gegnerische, und die Hälfte der eigenen Truppen liegt in Garnisonen. | 17.11. (eigener Befund) | im Test (`townWorth`, `refillShare`, `supplyRuns`, `campaignEdge`, `freshHeroes`, `townHop`, `scouts`). Offen: gegnerische Hauptarmee stellen, eroberte Städte halten (S6) |
| S5 | **Eine Stadt ist wert, was sie bringt** – an jedem Tag und für jeden Helden, nicht nur für den Haupthelden „im Feldzug“. Außerhalb des Feldzugs zählte eine gegnerische Stadt 20.000, weniger als eine Goldmine in Feindeshand (42.000): ein Hauptheld mit 400.000 Armee jagte 65 Tage Nebenhelden und Minen, während der Gegner vier Städte hielt. Die letzten Städte eines Gegners sind die Partie wert. | 17.11. (eigener Befund aus 549 Partien) | im Test (Schalter `townWorth`). Offen: Wert steigt, je weniger Städte der Gegner noch hat |
| S6 | **Alle Mittel einsetzen: Garnisonstruppen und Gold.** Am Tag 84 führt der Hauptheld im Mittel 249.000, in den Garnisonen liegen 313.000 (sieben Plätze je Armee: was nicht zu den sieben besten Sorten gehört, bleibt liegen), dazu 65.000–150.000 Gold ungenutzt und Kreaturen, die mangels freiem Platz nicht gekauft werden. Der Gegner führt alles in acht Helden. Städte wechseln rund zehnmal je Partie die Seite; angegriffene Garnisonen halten in 2 % von 2.895 Fällen und kosten 162.000 je Partie für 56.000 beim Gegner. | 17.11. (eigener Befund) | erste Fassung gebaut (Schalter `secondArmy`, aus): zwei Probepartien zeigen, dass das Maß noch nicht stimmt (stärkster gesehener Gegnerheld schwankt zwischen 14.000 und 450.000). Nächster Schritt: mehrere Nebenhelden mit Truppen, die leere und schwach besetzte Städte nehmen (`scouts` + `townWorth` + `collectorsTakeGarrison`), Nächte nach Möglichkeit in Städten |
| S7 | **Ein Held steht nie tatenlos herum.** Ein Hauptheld stand 28 Tage mit vollen Bewegungspunkten im Freien, weil ein gleich starker Gegner in Reichweite war; daheim warteten 187.000–370.000 Truppen. | 17.11. (eigener Befund) | behoben (`trappedHome`, Standard an): er marschiert in die Stadt, in der er sicher ist. Offen: Hauptheld, der in der Stadt festsitzt, weil draußen alles „unsicher“ ist |
| S8 | **Der Hauptheld wird nachversorgt.** Mit dem neuen Stadtwert griff er 50 Tage lang täglich an und gewann alles, wurde aber nie aufgefüllt: am Ende 230.000, daheim 325.000 (12 Titanen, 24 Naga-Königinnen) und 97.000 unkaufbar. | 17.11. (eigener Befund) | im Test (`supplyRuns`, `refillShare` – wirkt jetzt auch außerhalb des Feldzugs) |
| S9 | **Wer im Tor steht, kämpft vor den Mauern.** Spielregel: Hält ein Held die Garnison und ein zweiter steht im Tor, trifft der Angreifer zuerst den im Tor – auf offenem Feld, allein, ohne Mauern und ohne die Garnison. FairAI rechnete ihm bisher Mauern und Garnison zu; in mitgeschriebenen Partien gingen so Armeen von 23.000 bis 104.000 im Tor eigener Städte verloren. | 18.11. (eigener Befund) | im Test (Schalter `nightWatch`: Kann abends mehr kommen, als der Held im Tor schlägt, geht der bessere Anführer hinter die Mauern und nimmt die besten Truppen beider; am Morgen hat jeder seine Truppen wieder. Probepartie: Stadt gehalten, Tausch und Rückgabe stimmen. Offen: der Rest über sieben Sorten bleibt draußen) |
| S10 | **Eine verlorene Armee freikaufen, wenn Gold übrig ist.** Kapitulation kostet den Rekrutierungspreis der Armee; der Held wartet mit ihr bis zum Wochenende in der Taverne. | 18.11. (eigener Befund) | im Test (Schalter `surrender` Stufe 2: nur echte Armeen, nur mit Gold, das nach dem Kauf aller wartenden Truppen übrig bleibt, nie am letzten Wochentag ohne eigenen Zug, und das Tor wird für den Rückkehrer frei gemacht – in der ersten Probepartie waren beide Städte besetzt und 66.000 Gold verfielen) |
| S11 | **Der beste Held führt die Armee.** Der Hauptheld flieht aus einer verlorenen Schlacht mit Stufe, Zaubern und Artefakten und wartet bis zum Wochenende in der Taverne. Bisher holte ihn niemand zurück: Die Truppen daheim führte, wer gerade die meisten hielt – ein Held der Stufe 1 (in einer Partie verlor so einer 173.000 gegen 222.000, der Gegner verlor 4.000; sein Vorgänger der Stufe 12 saß in der Taverne). | 18.11. (eigener Befund) | **Standard seit 18.11.** (`veterans` 2, seltene Lage, in gestellten Partien geprüft – `FAI_TEST_FLEE`): Ein erfahrener Held aus der Taverne wird sofort angeheuert, wenn er klar der bessere Anführer ist (niemand führt, oder fünf Stufen über dem jetzigen und ein Zehntel mehr wert für die Armee) – dort, wo die meisten Truppen stehen; er übernimmt sie und führt eine Woche lang unangefochten, während der Rest zu ihm gebracht wird |
| S12 | **Zurückliegen heißt: die Schlacht suchen.** In verlorenen Partien war unsere Hauptarmee bis Tag 84 die größere und wurde nie eingesetzt; wir schlagen große gegnerische Armeen erst bei 3,5-facher Überlegenheit, der Gegner unsere bei 1,6-facher. | 18.11. (eigener Befund) | im Test (Schalter `boldBehind`: Für jede Stadt, die der Gegner mehr hält – Diebesgilde und gesehene Städte –, sinkt der verlangte Vorsprung gegen einen Gegnerhelden um ein Zehntel, von 1,3 bis 1,05. Zählwerk dazu: Tage, an denen ein mindestens gleich starker Gegnerheld in Tagesreichweite stand und nicht angegriffen wurde) |
| S13 | **Die letzte Burg nicht mit dem letzten Helden verlassen**, sondern den Heimvorteil nutzen. | 18.11. | **Standard seit 20.11.** (Regel `lastStand`: Ist nach dem Verlust einer Stadt nur noch eine übrig und wurde in den letzten zehn Tagen irgendwo eine Armee gesehen, die den Haupthelden um mehr als das 1,2-Fache übertrifft, beendet er jeden Tag in dieser Stadt – von draußen geht er heim, von der Stadt aus macht er nur Wege, die am selben Tag zurückführen. In einer gestellten Partie geprüft. Was in der Partie geschah: Der letzte Held zog mit 17.203 an Tag 31 los, um die zweite Stadt zurückzuholen – der Hauptheld des Nutzers, am selben Tag mit 47.000 gesehen, stand 40 Felder entfernt und galt dem Reichweitenmodell nicht als Gefahr für die Hauptstadt. In der Hauptstadt blieben 5.763 und ungekaufte Kreaturen im Wert von rund 40.000 – zusammen wären es rund 60.000 hinter Mauern gegen 67.000 gewesen. Bewusst ausgenommen ist die eine Stadt der ersten Wochen: Dort hielte jeder stärkere Held, der vorbeikommt, den eigenen zehn Tage zu Hause) |
| S14 | **Keine Stadt mit Türmen leer lassen:** „zumindest 1 Tier-1-Einheit wäre eine gute Idee gewesen, da so die 3 Türme einmal angreifen können.“ | 18.11. | **Standard seit 20.11.** (`towerGuard`: In einer Stadt mit Zitadelle oder Burg wird die billigste Kreatur gekauft, und ein Held, der die Truppen mitnimmt, lässt eine Kreatur zurück. Ein Fort allein hat keine Türme. Allein gemessen in 347 Partien gegen Nullkiller2: +0,05 ± 0,08, 31 statt 40 Niederlagen, an Tag 42 im Mittel 0,17 Städte mehr) |
| S15 | **Die 7-Tage-Regel berücksichtigen:** Ohne Stadt ist das Spiel nach sieben Tagen verloren. | 18.11. | teilweise (ohne Stadt zählt jede Stadt „alles“; in der Partie pendelte der letzte Held trotzdem fünf Tage durch ein Tor, statt eine der beiden Städte anzugreifen – Ursache offen) |
| S16 | **Skelettfarm:** viele verlustarme Kämpfe für die Nekromantie, Skelettwandler nutzen. | 18.11. | offen (Nekromantie zählt als Fähigkeit, Skelette fallen nebenbei an) |
| S17 | **Dämonenfarm:** bewusst schwache Stufe-1-Einheiten im Kampf opfern, um sie mit den Grubenlords als Dämonen wiederzubeleben. Dazu: Futter nach Trefferpunkten pro Gold einkaufen, passende schwache Wachen als Farmkämpfe aussuchen, den letzten Gegner stehen lassen, bis alle Grubenlords gezaubert haben. | 18.11. | offen |
| S18 | **Freie Schätze einsammeln:** Rohstoffe und Schätze, die schon frei zugänglich liegen, nicht liegen lassen. | 22.11. (zweite Partie des Nutzers) | offen (Protokoll, 23.11.: Ab Woche 2 fanden die Helden oft „kein Ziel“. Abgelehnt wurden bis zu 33 Ziele auf einmal als „unsicher“ – in Reichweite des Haupthelden des Nutzers – und bis zu 58 als „hinter einer Wache“. Dazu fehlten Sammler: ab Tag 19 zwei Helden, ab Tag 24 einer, an Tag 31 keiner, und am Tagesende lagen 0 bis 50 Gold in der Kasse, also kein Geld für einen neuen. Zu ändern: „unsicher“ ist kein Verbot, sondern eine Abwägung – ein Sammler ohne Armee riskiert nur sich selbst; und prüfen, ob gemerkte Wachen noch stehen) |
| S19 | **In der letzten Burg bleiben, bis der Gegner wirklich schwächer ist** – und nicht jeden Tag die Bewegungspunkte mit Hinaus- und Hineinreiten verbrauchen. | 22.11. | teilweise (`lastStand` hielt den Haupthelden von Tag 33 bis 42 in der Stadt. An Tag 43 ließ die Regel los: Nach einem Truppenkauf lag der Gegner nur noch beim 1,07-Fachen statt über dem 1,2-Fachen, der Held – Stufe 1 mit 111.786 – ritt zu einem Ziel und blieb im Freien stehen, weil er sich dort für stärker hielt („Verteidigung 150.874 gegen Bedrohung 137.747“). Die Armee des Nutzers war mit 99.020 geschätzt und hatte 143.133. Zu beheben: die Stadt erst verlassen, wenn wir im Feld klar gewinnen; die Schätzung fremder Armeen; die eigene Stärke im Freien) |
| S20 | **Kapitulieren vor Fliehen:** Flucht wirft die ganze Armee weg. Erster Gedanke: freikaufen („surrender“) – Gold zahlen, Held und Armee behalten. Reicht das Gold nicht: kämpfen, bis es reicht, oder erst fliehen, wenn der größtmögliche Schaden angerichtet ist und der Held noch sicher entkommt. | 22.11. | offen im Standard (FairAI flieht: in der Partie auf „Back for Revenge“ mit 510.447 gegen 1,25 Mio. nach einer Runde, in der Zufallskarte Coronius mit 10.209. Der Schalter `surrender` 2 – freikaufen ab 20.000 Armee, wenn Gold übrig ist – ist gebaut und wartet auf einen Test; „kämpfen, bis es reicht“ und „erst Schaden anrichten“ fehlen) |
| S21 | **Geflohene Helden sofort zurückkaufen** – zumindest den Haupthelden und wichtige Nebenhelden. | 22.11. | offen (Coronius, Hauptheld bis Tag 17, wurde nach seiner Flucht nie zurückgeholt; angeheuert wurden nur neue Helden der Stufe 1. Ursache, 23.11.: kein Gold – an den Tagen nach der Flucht lagen 5 bis 40 Gold in der Kasse, der Rückkauf kostet 2.500. Zu ändern: eine Rücklage für den Rückkauf halten, solange der Hauptheld im Feld steht, und vor Flucht oder Freikauf das Gold dafür einrechnen) |
| S22 | **Garnisonen angreifen** (gewöhnliche und magische), wenn die Armee dafür reicht. | 22.11. | offen (im Code zählt eine fremde Garnison als Tor mit 800 bis 1.500 Gold Wert plus Erfahrung und dem unbekannten Land dahinter; der Kampf wird wie eine Kartenwache geschätzt. Das ist zu wenig, wenn dahinter schon bekanntes Land oder der Gegner liegt. Magische Garnisonen – kein Zaubern – kennt die Schätzung nicht) |
| S23 | **Werften und Schiffe nutzen.** An Land gehen und an Bord gehen verbraucht jeweils den Rest der Bewegungspunkte – das ist einzurechnen. | 22.11. | teilweise (FairAI kauft ein Schiff nur, wenn dem Haupthelden oder einem Kundschafter „das Land ausgeht“ und noch viel Karte unbekannt ist; in der Partie auf „Back for Revenge“ kein einziges Mal. Dass Ein- und Aussteigen den Rest des Tages kostet, ist noch zu prüfen. Auf Inselkarten kennt FairAI an Tag 56 32 % der Karte, der Gegner 63 %) |
| S24 | **Fehlermeldung des Servers:** „Player PURPLE has to answer queries before attempting any further actions … CBlockingDialogQuery“ (erste Runde, Karte „Back for Revenge“). | 22.11. | offen (Protokoll: Rückfrage 287 – eine Ja/Nein-Frage an einem Ort – und gleich darauf ein Rekrutierungsfenster 288; FairAI antwortete auf das zweite, bevor der Server die erste Antwort verbucht hatte. In dieser Partie ohne Folgen. Zu ändern: Antworten streng nacheinander abwarten) |

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

> „noch ein hinweis: life leech von vampire lords funktioniert nicht gegen elementals oder non-living
> creatures. auch soetwas muss berücksichtigt werden"

> „schreibe dir alles, was ich an wichtigen punkten festhalte irgendwo auf github auf, damit du es
> nicht vergisst und alle punkte bei der fertigen KI dann berücksichtigt werden. auch die sachen, die
> ich in den vergangenen tagen immer wieder angemerkt habe"

Die Punkte vom 15. und 16.11. sind oben sinngemäß eingetragen (Quelle: Gesprächsnotizen und Roadmap
des Projekts). Fehlt etwas oder ist etwas falsch wiedergegeben, wird es hier nachgetragen.

## Wörtliche Anmerkungen des Nutzers vom 18.11.2026 (nach seiner ersten Partie gegen FairAI)

> „obwohl sie mich kommen sah, hat sie wenige einheiten in ihrer burg rekrutiert. sie ist außerdem
> mit ihrem letzten helden aus ihrer letzten burg geflohen statt in ihr zu bleiben und den
> heimvorteil zu nützen. generell hatte sie das ganze spiel über wenige einheiten und in ihrer burg
> konnten sehr viele rekrutiert werden als ich sie einnahm. ich konnte sie auch ohne kampf einnehmen,
> da sie keine einheiten zur verteidigung zurückgelassen hat. zumindest 1 tier 1 einheit wäre eine
> gute idee gewesen, da so die 3 türme einmal angreifen können."

> „außerdem habe ich den eindruck, dass sie bei kämpfen gegen neutrale einheiten nicht durchrechnet,
> wie sie diese mithilfe ihrer bogenschützen, vielen 1er stacks an einheiten und defensiv spielen
> gewinnen kann ohne verluste zu erleiden oder nur 1-2 1er stacks dabei verliert."

> „es könnte auch sein, dass die KI noch nicht berücksichtigt, dass sie nach 7 tagen ohne burg
> automatisch verliert."

> „aber sachen wie demon- und skelettfarmen, bessere battle AI die kämpfe verlustfrei gewinnen kann
> wenn es einen weg gibt und das verwenden aller zauber kommt noch, oder?"

> „bei demonfarming opfert man bewusst schwache tier 1 einheiten im kampf, um sie als dämonen
> wiederzubeleben"

Die Partie (Zufallskarte, 1 gegen 1, 36 Tage, FairAI mit Conflux) ist ausgewertet in der Roadmap des
Projekts, Abschnitt „Erste Partie des Nutzers“.

## Wörtliche Anmerkungen des Nutzers vom 22.11.2026 (nach zwei Partien gegen die Fassung vom 20.11.)

> „es ist eine merkliche verbesserung zur letzten, hat aber noch klare schwächen. sie hat viele schätze, die
> bereits frei und sammelbar waren, einfach liegen gelassen."

> „als mein deutlich stärkerer held um ihre letzte burg geritten ist, hat sie sich lange korrekt eingebunkert,
> dabei aber jeden tag ihre bewegungspunkte durch ständiges raus- und reinreiten verbraucht. kein fehler, aber
> könnte irgendwann mal schieflaufen. komischerweise hat sie sich irgendwann dann aber doch hinreißen lassen und
> ist mit beiden helden rausgeritten, obwohl mein held noch nah an ihrer burg war."

> „ich vermute auch, dass sie ihren stärksten helden, coronius, nicht wiedergekauft hat, nachdem sie mit ihm
> geflohen ist."

> „die KI sah sich in einem riesigen kampf im nachteil und ist nach 1 runde geflohen. das problem ist, dass sie
> dabei ihre gesamte armee verloren hat. fliehen ist nicht sinnvoll, wenn man dabei eine große armee einfach
> wegwirft. der erste gedanke, wenn man armee und held retten will, sollte sein, dass man ‚surrender' verwendet"

> „hat man das gold nicht, kann man entweder kämpfen bis es leistbar ist oder fliehen, sobald man die
> größtmögliche menge an schaden angerichtet hat und der held noch sicher entkommen kann. wichtig ist auch, dass
> die KI nicht vergisst, sofort ihren geflohenen helden in der taverne zurückzukaufen - zumindest wenn es ihr
> hauptheld oder ein wichtiger nebenheld ist."

> „die KI hat auch nie eine garrison angegriffen (weder normale noch magische), selbst als sie die armee dazu
> hatte. shipyards scheint sie auch zu ignorieren"

> „sobald sie boote kauft, ist es wichtig, dass sie berücksichtigt, dass an landgehen und anboard gehen jeweils
> den rest der bewegungspunkte verbraucht."

> „all das sind nur infos für dich zum auswerten … es soll nicht die aktuellen prioritäten ändern, sondern nur
> impulse für die weiterentwicklung der adventure map KI geben, wenn du wieder an ihr arbeitest."

Die Partien: Zufallskarte, 1 gegen 1, 45 Tage (die erste endete an Tag 36), und „Back for Revenge" mit sieben
FairAI-Spielern. Protokolle ausgewertet am 22.11.

## Wörtliche Anmerkungen des Nutzers vom 23.11.2026 (Kampfsimulationen und Testkämpfe)

> sag mir noch, ob du auf meinem zweitrechner auch akkurate simulationen von kämpfen durchführen kannst, und zwar in 3 modi: exakt so wie im spiel, ohne moral/glück mechanik und mit festem RNG seed. wenn ich dann z.B. einen kampf vorgebe, der ohne verluste und/oder ohne magie gewonnen werden soll, könntest du ihn dann so lange mit anderen herangehensweisen simulieren lassen bis du eine lösung findest? auch MP könnten nämlich in gewisser weise als temporärer verlust zählen, wenn man sie in einem wichtigeren kampf danach nicht mehr hat.

> was für kampfsimulationen hast du im rahmen dieses projektes bislang gemacht? wären randomisierte kämpfe ähnlich großer armeen, bei denen du beide seiten steuerst und die beste strategie und taktik für beide findest, möglich?

> simuliere und plane folgende testszenarien aus meinen bildern. ignoriere dabei helden - tu so, als ob die linke seite keinen hätte. spiele in den kämpfen beide seiten und versuche, mit so wenigen verlusten wie möglich so viel schaden wie möglich anzurichten. es ist nicht erlaubt, die stacks umzuverteilen oder zu splitten. wichtig: auch die umgebung soll gleich sein (sträucher, berge, etc.). bild 1 (es sind 5 zwerge in der armee): gegen die aktuelle version von FairAI ist es möglich, mit der linken seite ohne verluste zu gewinnen, weil die gnolle unter den richtigen bedingungen ewig warten. spiele beide seiten durch und finde für beide die optimale strategie. bild 2: dieser kampf muss verlustfrei für die vampire lords gewonnen werden, egal, wer die dendroid soldiers steuert. finde diesen weg. bild 3: dieser kampf muss verlustfrei für die vampire lords gewonnen werden, egal, wer die hydras steuert. finde diesen weg. bild 4: dieser kampf kann von beiden seiten gewonnen werden, aber die linke ist im vorteil. spiele beide seiten und finde die besten strategien für beide. bild 5: dieser kampf kann immer von den death knights gewonnen werden, aber es ist unklar, ob sie dabei verluste vermeiden können. finde den besten weg für die linke seite, verluste bei ihnen anzurichten.

> hol dir auch die karte "with sword and fire". ihr gimmick ist, dass jeder kampf ohne verluste gewonnen werden muss, um weiterzukommen. dabei gibt es keinen RNG aufgrund der art und weise, wie die karte aufgebaut ist. versuche, sie zu schaffen bzw. die lösungen für alle kämpfe zu finden
