# mrmac Migration Plan

## Zielsetzung
Das langfristige Architekturziel ist es, die mrmac-Stackmaschine von einer "angehängten Makrosprache" zum Kern des Editors auszubauen (ähnlich dem Lisp-Maschinen-Ansatz von Emacs). Anstatt komplexe Logik in C++ hart zu codieren, stellt C++ zukünftig idealerweise nur noch die atomaren Primitiven bereit, während die Editor-Logik als mrmac-Bytecode ausgeführt wird.

## Programmtechnische Herangehensweisen (Optionen)

### Option 1: Inkrementeller "Bottom-Up" Ansatz (Empfehlung)
Wir behalten die aktuelle Turbo-Vision (tvision) Event-Schleife und den `MRCommandRouter` vorerst bei. Wir erweitern die VM um fehlende C++-Primitiven (z. B. atomare Pufferzugriffe). Nach und nach werden bestehende C++-Editorfunktionen als mrmac-Makros neu geschrieben. Der `MRCommandRouter` leitet dann Befehle (z.B. `cmMrTextUpperCaseMenu`) an die VM weiter.
* **Vorteile:** Schrittweiser Übergang, geringes Risiko. Bestehende Tests laufen weiter. Architekturverträge (insb. `vm-tvcall-contract.md`) bleiben intakt, da C++ weiterhin das Rendern übernimmt.
* **Nachteile:** Der C++-Command-Router bleibt in der Übergangszeit als Verteiler erhalten und muss gepflegt werden.

### Option 2: "Top-Down" Event-Driven Ansatz (Das Emacs-Modell)
Fast alle Events (`evKeyDown`, `evCommand`) werden direkt an die VM durchgereicht. Die VM enthält einen in mrmac geschriebenen Main-Dispatcher.
* **Vorteile:** Konsequente und schnelle Umsetzung der Vision ("VM als Editor").
* **Nachteile:** Sehr invasiv. Bricht potenziell das gesamte Keymap-System und erfordert massive UI-Umbauten. Hohes Risiko, bestehende Funktionen zu zerstören.

### Option 3: TVCALL-getriebene UI-Verlagerung
Fokus darauf, zunächst Dialoge und UI-Interaktionen über `TVCALL` in die VM zu ziehen, bevor die Textmanipulation verlagert wird.
* **Vorteile:** Zeigt schnell das Potenzial der VM für die UI-Steuerung.
* **Nachteile:** Verletzungsrisiko des `vm-tvcall-contract.md` (Verbot direkter Render-Nutzungen), da `TVCALL` laut Vertrag primär für verzögerte (deferred) UI-Befehle gedacht ist.

## Begründete Empfehlung
**Ich empfehle Option 1.** Sie respektiert die geschützte Architektur und erlaubt einen evolutionären Übergang ohne "Big Bang". Wir bauen eine C++-"Standardbibliothek" von Primitiven für mrmac auf und reduzieren sukzessive C++-Code, wo Makros die Arbeit besser (und anpassbarer) erledigen können. Dies minimiert das Risiko und hält das System jederzeit lauffähig und testbar.

---

## Funktionale Bereiche zur Verlagerung auf die Stackmaschine

Die folgenden Bereiche eignen sich hervorragend für eine Implementierung in mrmac, sobald die grundlegenden Puffer-Intrinsics verfügbar sind:

1. **Text- und Absatzformatierung:** (Groß-/Kleinschreibung, Zentrieren von Zeilen, Absatz-Reformatierung). Dies sind primär Puffer-Lese/Schreib-Operationen.
2. **Block-Operationen & Einrückung:** (Indent/Undent von Blöcken). Iteration über Zeilen und Modifikation lassen sich gut in mrmac-Skripten abbilden.
3. **Erweiterte Cursor-Navigation:** (Wortsprünge `macdWordLeft`/`macdWordRight`, Klammer-Paare matchen). Die Logik, was ein "Wort" ist, lässt sich leichter in einem Makro konfigurieren.
4. **Suchen und Ersetzen:** Komplexe Suchlogik kann in mrmac abgebildet werden. Der Dialogaufruf erfolgt über isolierte Primitiven, die eigentliche Puffer-Suche in mrmac.
5. **Einfaches Fenster-Management:** (Cascade, Tile, Window Next/Prev).

---

## Vorgeschlagener Umsetzungsplan für Option 1

1. **Phase 1: Inventur und Primitiven-Aufbau**
   - Analyse: Welche Intrinsics fehlen für vollständige Puffer-Manipulation (Cursor lesen/setzen, Text selektieren, einfügen/löschen)?
   - Implementierung dieser atomaren Intrinsics in `mrmac/MRVM.cpp` und Sicherstellung durch VM-Regressionstests.
2. **Phase 2: Etablierung einer mrmac "Standardbibliothek"**
   - Schaffung eines Mechanismus, um eine Basis-Set von Makros beim Editor-Start zuverlässig zu laden (z.B. über `MRSETUP` oder einen dedizierten Mechanismus).
3. **Phase 3: Migration der Text-Formatierung (Proof of Concept)**
   - Neuschreiben von einfachen Befehlen (z.B. `cmMrTextUpperCaseMenu`) in der mrmac-Sprache.
   - Anpassen des `MRCommandRouter`, sodass diese Befehle an die VM delegiert werden (z.B. als virtueller Tastendruck auf ein gebundenes Makro).
   - Entfernen des alten C++-Codes für diese Funktionen.
4. **Phase 4: Migration von Block- und Navigationsoperationen**
   - Sukzessive Überführung komplexerer Befehle (Einrückung, wortweise Navigation) in mrmac.
5. **Phase 5: Evaluierung komplexerer Interaktionen**
   - Ausbau der VM-Fähigkeiten für Interaktion (wie Suchen/Ersetzen), immer unter strikter Einhaltung des `vm-tvcall-contract.md`.