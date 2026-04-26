1. UI-Primitive-Schicht: Was fehlt noch?

Die aktuelle Schicht ist tragfähig, aber noch nicht vollständig als allgemeines UI-System in mrmac.

Was bereits da ist:

UI_DIALOG
UI_LABEL
UI_BUTTON
UI_INPUT
UI_LISTBOX
UI_DISPLAY
UI_EXEC
Was aus meiner Sicht noch fehlt:

Layout-/Container-Semantik
heute ist das UI noch stark koordinatengetrieben
es fehlen saubere Gruppen/Container/Spalten/Zeilen
das wird wichtig, sobald Dialoge größer oder dynamischer werden
Zustandsänderung nach Erzeugung
eine tragfähige UI-Sprache braucht mehr als nur Erzeugen und einmaliges Ausführen
es fehlen klare Set-/Get-Operationen für:
Text
Enabled/Ghosted
Visible
Focus
Auswahlindex
Listeninhalte
UI_DISPLAY war ein Schritt in diese Richtung, aber noch nicht das ganze Modell
Nichtmodale UI
wir hatten das Thema bereits
aktuell ist UI_EXEC logisch modal
für Toolfenster, Manager, Monitor-Views oder ein späteres Calculator-/Inspector-Fenster fehlt ein sauberer modeless Sprachpfad
Weitere generische Widgets
sinnvoll wären mittelfristig:
Checkbox
Radio Cluster
History/Combo
Multi-column list / table
eventuell Scrollview/Viewport
nicht als Selbstzweck, sondern weil echte Setup- und Tooldialoge diese Bausteine brauchen
Dialogzustand und Ereignismodell
heute ist das Modell noch stark “Dialog bauen, laufen lassen, Resultat lesen”
für eine echte UI-Sprache fehlt ein klarer Event-/Command-Gedanke:
welcher Button feuert welchen Rückgabecode
wie reagieren abhängige Controls
wie wird Validierung sprachlich ausgedrückt
Meine Empfehlung dazu:
Nicht sofort alles bauen. Der richtige nächste Ausbau wäre:

Set/Get-Zustand
Enabled/Visible/Focus
danach Checkbox/Radio
danach modeless


2. Moderne Sprachvision von mrmac als Zentrum: Was fehlt noch?

Hier geht es nicht nur um UI, sondern darum, dass mr wirklich vom Makroprozessor getragen wird.

Aus meiner Sicht fehlen dafür vor allem diese Dinge:

Ein vollständigeres Systemmodell in der Sprache
mrmac kann schon viel, aber noch nicht alle zentralen Editorzustände elegant ausdrücken
es fehlen noch mehr direkt nutzbare Sprachzugriffe auf:
Fensterzustand
Blockinhalte/-metadaten
Menüs
Dialog-/UI-Zustände
Workspace-/Desktop-Zustände
Makrogesteuerte UI- und Menümanipulation
das hatten wir fachlich schon richtig eingeordnet
wenn mrmac wirklich Zentrum sein soll, müssen Menüs, Toolfenster und Kommandoflächen sprachlich formbar sein
nicht über Nebenregister, sondern über den zentralen K/V-Hash und saubere Projektion
Bessere Komposition größerer Makrosysteme
Multi-Makro-Dateien gehen bereits
aber für die Sprachvision fehlen noch Dinge wie:
sauberere Modul-/Bibliotheksnutzung
wiederverwendbare UI-/Helper-Makros
klarere Struktur für größere Makropakete
Stabilere Sprachoberfläche für moderne Erweiterungen
wir haben schon moderne Primitive ergänzt
was noch fehlt, ist eine bewusst definierte Grenze:
was ist kanonischer Kern
was ist moderne MRMAC-Erweiterung
nicht aus Historiengründen, sondern um die Sprache für künftigen Ausbau stabil zu halten
Mehr Selbstbeschreibung und Werkzeugunterstützung
wenn mrmac Zentrum sein soll, braucht man mittelfristig mehr als nur “Compiler kompiliert”
sinnvoll wären später:
bessere Compile-Diagnosen
klarere Fehlermeldungen an der Sprachoberfläche
vielleicht Hilfen für Makroautoren bei UI-/Binding-/Menüstrukturen
Meine Empfehlung dazu:
Der wichtigste nächste Schritt für die Sprachvision ist nicht noch mehr Einzelbefehle, sondern:

UI-Primitives vervollständigen
Menümanipulation über K/V-Hash
Set/Get-/State-Modell für UI und Editorzustände
Damit würde mrmac wirklich vom „mächtigen Makrosystem“ zum führenden Systemkern werden.