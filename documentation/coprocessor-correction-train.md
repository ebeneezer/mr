# Korrekturzug: nicht blockierende Worker- und Warm-up-Architektur

Stand: 2026-07-22
Branch: `fix/minimap-background-warmup`
Status: abgeschlossen und maintainer-seitig abgenommen

Dieses Dokument hält die verbindlichen Prämissen, die Unterzüge, ihre Prüfziele sowie die nachgewiesenen Umsetzungen und Einschränkungen fest. Es ist kein Architekturvertrag. Der bestehende Inhalt des Technical Manual bleibt bis zu einer späteren Entscheidung unverändert und gilt nicht als Nachweis für die tatsächliche Implementierung.

Eine Änderung erhält in diesem Protokoll erst den Status **nachgewiesen**, wenn Implementierung, Instrumentierung, Messung und Sichtprüfung übereinstimmen. Vorhandener Code auf dem experimentellen Branch ist allein durch seine Existenz weder akzeptiert noch nachgewiesen. Maintainer-Abnahmen gelten für den jeweils vorgelegten Funktionsstand und werden nicht durch spätere interne Auditbezeichnungen erneut geöffnet.

## 1. Verbindliche Prämissen

### 1.1 Worker-Erzeugung und Core-Zuordnung

1. Eine Lane klassifiziert Arbeit. Sie ist weder ein einzelner Worker noch ein Serialisierungspunkt.
2. Eine ausführbare Arbeitseinheit erhält einen eigenen Worker. Unabhängige Arbeitseinheiten verschiedener Fenster, Panes, Pipelines und Generationen dürfen nicht hinter einem gemeinsamen Lane-Worker warten.
3. Jeder erzeugte Worker erhält einen monotonen globalen `workerOrdinal`.
4. Die Core-Zuordnung erfolgt ausschließlich als mathematisches Round Robin:

   ```text
   assignedCore = allowedCores[workerOrdinal % allowedCoreCount]
   ```

5. Die Anzahl der Worker ist nicht auf die Anzahl der verfügbaren Cores begrenzt. Existieren mehr Worker als Cores, werden die Cores weiterhin strikt modulo belegt.
6. Die Zuordnung eines Workers bleibt für seine Lebensdauer stabil. Eine erneute Affinitätswahl bei jeder Task-Aktivierung ist nicht zulässig.
7. Die Erzeugung, Zuordnung, Paketübernahme, Ergebnisabgabe und Terminierung jedes Workers muss instrumentiert und im Performance Panel sichtbar sein.
8. Das Identitätsmodell wird auf fachlich unabhängige Lebenszyklen begrenzt. Owner, Auftrag, Worker-Ausführung und OS-Thread dürfen nur dann eigene Bezeichner besitzen, wenn sie nicht dieselbe Lebensdauer ausdrücken. `workerOrdinal` ist der einzige kanonische Bezeichner einer Worker-Ausführung. Ein zweiter Worker-Zähler ist unzulässig.
9. Summen wie `created`, Plattformwerte wie die OS-Thread-ID und Ressourcenwerte wie die Core-ID sind keine fachlichen Identitäten. Das Performance Panel muss diese Kategorien sprachlich und visuell voneinander trennen.

### 1.2 Ownership und Parallelität

1. Jedes Editorfenster und jedes Bento-Pane ist ein eigener Execution Owner.
2. Ein Owner kann gleichzeitig mehrere Worker besitzen. Insbesondere sind BOF- und EOF-seitige Scans eigenständige Arbeitseinheiten.
3. Ein dedizierter Pane-Worker bedeutet einen ausschließlich diesem Pane und dieser Arbeitseinheit zugeordneten Worker. Es bedeutet nicht, dass ein unbeschäftigter OS-Thread ohne Auftrag dauerhaft erhalten werden muss.
4. Fokusverlust pausiert weder Scan noch Warm-up. Hintergrundfenster und Hintergrund-Panes nutzen weiterhin die ihnen modulo zugeordneten Cores.
5. Ein Cursor-Sprung erzeugt sofort eine neue Scan-Generation mit neuen BOF- und EOF-seitigen Arbeitseinheiten, soweit der Ledger dafür Lücken ausweist.
6. Bereits laufende Worker beenden ihr begrenztes Paket und terminieren anschließend. Sie dürfen nicht die neue Cursor-Generation blockieren.
7. Noch nicht begonnene, obsolete Pakete dürfen verworfen werden. Ergebnisse eines bereits laufenden Pakets dürfen bei identischer Dokumentversion den Ledger ergänzen, aber keine aktuelle Viewport-Projektion einer neueren Generation überschreiben.
8. Workspace Restore stellt weder Worker-Ordinale noch Threads wieder her. Er stellt Fenster, Panes und fachliche Zustände wieder her; daraus werden neue Execution Owner, Worker und Modulo-Zuordnungen deterministisch neu erzeugt.

### 1.3 Pakete, Ledger und Adoption

1. Scan- und Warm-up-Arbeit wird in endliche, messbare Pakete zerlegt.
2. Jeder fachliche Bereich besitzt einen domänenspezifischen Ledger der bereits verlässlich ausgewerteten Spans, Checkpoints und Versionen.
3. Es wird keine zusätzliche globale Registry und kein zweiter K/V-Store eingeführt. Ledger bleiben Bestandteil des jeweiligen abgeleiteten Zustands.
4. Paketakquisition darf parallel erfolgen. Adoption muss die fachlichen Abhängigkeiten des Ergebnisses beachten.
5. Ein Worker liest aus einem unveränderlichen Snapshot. Er mutiert weder TVision-Objekte noch den kanonischen Dokumentzustand.
6. Die UI übernimmt ausschließlich versionierte, unveränderliche Ergebnisse innerhalb eines begrenzten Zeitbudgets.
7. Zeichnen, Event-Dispatch und kontrollierte Ergebnisadoption verbleiben auf dem UI-Thread. Scan, Analyse, Projektion und teure Formatierung nicht.

### 1.4 C++18-Stil

1. Der Korrekturzug verwendet C++18-Stil. Es werden keine neuen C++20-Sprach- oder Bibliotheksstrukturen eingeführt.
2. Neu geschriebener oder im Korrekturzug grundlegend überarbeiteter Scheduler-Code verwendet insbesondere keine Concepts, Ranges, generischen Frameworks, `std::jthread` oder `std::stop_token`.
3. Worker-Lebensdauer wird mit `std::thread`, explizitem Stop-Zustand, `std::condition_variable` und sichtbarem Join verwaltet.
4. Es entstehen keine atomaren Wrapper. Ein erforderlicher atomarer Stop-Zustand bleibt ein direkt sichtbarer Bestandteil der konkreten Task- beziehungsweise Worker-Struktur.
5. Bestehende Vorkommen von `std::jthread` und `std::stop_token` im derzeitigen experimentellen Scheduler sind technische Schuld dieses Korrekturzugs. Sie gelten nicht als freigegebene Ausnahme.
6. Neue Sources werden ausschließlich an stabilen semantischen Grenzen angelegt und aussagekräftig benannt. Mechanische `Helper`-, `Util`- oder `Impl`-Dateien sind ausgeschlossen.

## 2. Fachlich notwendige Ordnungsgrenzen

### 2.1 Zustandsbehaftete Syntax

Zustandsbehaftete Syntax kann nicht zuverlässig rückwärts ausgewertet werden. Das ist keine Begründung für einen einzelnen Worker, sondern für geordnete Adoption:

1. Der BOF-seitige Auftrag sucht den nächsten verlässlichen Syntax-Checkpoint beziehungsweise einen ausreichend frühen Kontextanker.
2. Fehlt ein verwendbarer Checkpoint, ist BOF der kanonische Anker.
3. Zeilenbereiche und Rohdaten dürfen parallel akquiriert werden.
4. Eine zuverlässige Syntaxauswertung läuft vom bestätigten `stateIn` vorwärts.
5. Ein vorwärts ausgewertetes Paket liefert neben Tokens seinen `stateOut`. Erst dieser Zustand validiert den Eintrittszustand des logisch folgenden Pakets.
6. Pakete dürfen vorläufig vorbereitet sein; ihre fachliche Adoption erfolgt jedoch in Dokumentreihenfolge oder ab einem bereits bestätigten Checkpoint.
7. Mehrere Fenster, mehrere bestätigte Checkpoint-Ketten und unabhängige Paketbereiche bleiben parallel ausführbar.

Akzeptierte Einschränkung: Die Zustandsabhängigkeit erzwingt eine geordnete Validierungsfront, aber keinen globalen Syntax-Worker.

### 2.2 Folding

Folding besitzt ebenfalls vorwärts wirkenden Strukturzustand:

1. Paketbereiche und erforderliche Syntaxdaten dürfen parallel akquiriert werden.
2. Ein Fold-Checkpoint enthält den für die Fortsetzung erforderlichen Strukturzustand.
3. Strukturelle Ergebnisse werden ab einem bestätigten Checkpoint vorwärts validiert.
4. Die Adoption erfolgt in Strukturreihenfolge. Unabhängige Dokumente, Fenster und bestätigte Checkpoint-Ketten bleiben parallel.

Akzeptierte Einschränkung: Die Strukturabhängigkeit erzwingt geordnete Validierung, aber keinen globalen Folding-Worker.

### 2.3 Externe Streams und Exec Sessions

Der Begriff **externer Stream** bezeichnet nicht automatisch eine MRMAC Exec Session. Drei Lebenszyklen sind auseinanderzuhalten:

1. Eine MRMAC Exec Session ist ein logischer VM-Ausführungskontext. Ihre endliche Rechenarbeit läuft als `MacroJob`. Eine pausierte oder auf UI-Adoption wartende Session darf keinen Worker blockierend festhalten.
2. Ein externer Prozessauftrag, beispielsweise Build- oder Shell-Ausführung, besitzt einen Prozesslebenszyklus und gegebenenfalls blockierende stdout-/stderr-Pipes. Solange diese Quelle lebt, darf ein ausschließlich ihr zugeordneter Worker erhalten bleiben.
3. Ein registrierter Live-Stream der Arten File, Journal, Device, Network oder Pipe besitzt einen Quellenlebenszyklus. Im aktuellen Code ist der Journal-/Log-Viewer der konkrete Nutzer von `registerExternalSource()` und `Lane::Extern`.

Die zulässige Ausnahme lautet vollständig:

- Eine Quelle, deren Betrieb technisch in `poll()`, `read()`, Prozess-Wait oder einem gleichwertigen externen Warten besteht, darf für ihre Lebensdauer einen dedizierten Worker behalten.
- Dieser Worker darf keinen anderen Owner und keine andere Lane-Arbeit serialisieren.
- EOF, explizites Schließen, Abbruch, Fehler und Prozessende müssen die Quelle deregistrieren, den Worker aufwecken, joinen und als sichtbares Terminierungsereignis protokollieren.
- Lang laufende Prozess-Streams, die heute nur als allgemeine `ExternalIo`-Task auf einer I/O-Lane erscheinen, werden auf dieselbe explizite Quellenlebensdauer geprüft. Ein faktisch dauerhafter Stream darf nicht als gewöhnliches endliches Paket getarnt bleiben.
- Erzeugt eine MRMAC Exec Session eine externe Quelle, kann die Session deren Owner sein. Worker- und Session-Lebensdauer bleiben dennoch getrennt: Die Session beendet oder storniert die Quelle; allein die Existenz einer Exec Session rechtfertigt keinen festgehaltenen Worker.

Damit bezieht sich die Ausnahme nur dann auf eine „Exec Session“, wenn damit ein externer Prozess- oder Terminalkanal gemeint ist. Für die im Projekt so benannte MRMAC Exec Session gilt sie nicht pauschal.

### 2.4 Unvermeidbare UI-Thread-Arbeit

Folgende Arbeit bleibt technisch an den UI-Thread gebunden:

- TVision-Zeichnen und Event-Dispatch,
- Mutation von TVision-Objekten,
- kontrollierte Adoption eines bereits berechneten Ergebnisses,
- unmittelbare Eingabe-, Cursor- und Selektionssemantik.

Diese Ausnahme erlaubt keine Analyse oder unbeschränkte lineare Arbeit im UI-Thread. Jede Adoption erhält ein messbares Zeit- und Mengenbudget.

## 3. Bento- und HexBento-Prämissen

### 3.1 Bento-Panes

1. Jedes Pane erhält eine eigene fachliche Owner-Identität aus Owner-Art und ownerlokaler ID sowie eine eigene instrumentierbare Worker-Lebensdauer.
2. Teure Arbeit eines Panes wird nicht über den Worker eines Nachbar-Panes oder des aktiven Hauptfensters ausgeführt.
3. Verdeckte oder unfokussierte Panes arbeiten weiter, solange ihr Ledger offene Pakete enthält.
4. Pane-Schließung beendet alle zugehörigen Quellen und Worker sichtbar. Workspace Restore erzeugt die Pane-Owner und deren Arbeitsaufträge neu.
5. Gemeinsame Dokument-Snapshots dürfen geteilt werden. Abgeleitete Pane-Projektionen und deren Ledger bleiben owner-spezifisch.

### 3.2 HexBento

Die gegenwärtige Implementierung berechnet Teile der Projektion synchron in `MRHexPaneView::draw()`:

- Das Strings-Pane ruft für jede sichtbare Zelle `mrHexStringCellAt()` auf. Die Erkennung kann pro Zelle bis zu 512 Bytes vorwärts und rückwärts untersuchen.
- Das Inspector-Pane baut bei jedem Zeichnen seine vollständige Liste numerischer, Gleitkomma-, LEB128-, GUID- und Textdarstellungen neu auf.
- Hex-, Decimal-, Binary- und Octal-Panes formatieren ihre sichtbaren Bytes ebenfalls während des Zeichnens.

Zielzustand:

1. Jedes der Panes Hex, Strings, Inspector, Decimal, Binary und Octal besitzt einen eigenen Projection Owner.
2. Ein Worker berechnet eine unveränderliche sichtbare Projektion aus einem `ReadSnapshot`.
3. Der Projektionsschlüssel umfasst mindestens Dokument-ID, Dokumentversion, Pane-Rolle, Cursor-Projektionsrevision, Endianness, Record-Länge, Scrollbereich und Pane-Geometrie.
4. Cursor-, Scroll-, Geometrie- oder Dokumentänderungen erzeugen eine neue Projektionsgeneration. Ein laufender Worker beendet sein begrenztes Paket; nur eine noch passende Generation darf die sichtbare Projektion ersetzen.
5. `draw()` zeichnet ausschließlich eine bereits adoptierte Projektion und einen begrenzten Eingabe-Overlay. Es führt keine String-Suche, Inspector-Konvertierung oder vollständige numerische Projektion aus.
6. Schreiboperationen, Hit-Testing und Cursorbewegung bleiben UI-seitig; ihre abgeleiteten Anzeigen werden erneut als Worker-Paket berechnet.

### 3.3 Bento 1.5 Pane-Widgets

Bento 1.5 ergänzt jeden Layout-Leaf um eine geschlossene Widget-Maske. Die erste Fassung benennt ausschließlich `FoldGutter` und `MiniMap`; weitere Widgets erfordern eine explizite Erweiterung des Enums. Es gibt weder eine Widget-Registry noch frei benannte Typen oder einen zweiten K/V-Speicher.

Die Maske beschreibt die Pane-Komposition. Sie wird zusammen mit Leaf-ID, Pane-Rolle und Sichtbarkeit über den vorhandenen `WORKSPACE`-Pfad serialisiert. Das Format `bento=v1.5` erweitert einen Leaf von `id:role:visible` auf `id:role:visible:widgetMask`. Das bisherige Format `bento=v1` bleibt lesbar und erhält beim Import die bisherigen rollenspezifischen Standardwerte. Unbekannte Bits werden abgelehnt, damit ein neuerer Zustand nicht stillschweigend semantisch reduziert wird.

Serialisiert werden keine Fold-Spans, Minimap-Projektionen, Warmfenster, Worker-, Task- oder Owner-IDs. Diese Zustände werden nach Restore aus Dokument und Pane-Konfiguration neu erzeugt. Ein Widget erhält keine konkurrierende Owner-Identität; alle späteren Widget-Aufträge verwenden den Owner des zugehörigen Panes. Die Vorbereitung ändert die bestehende Folding-Auswertung nicht. Die vorhandene Minimap-Suppression wird beim Restore bereits aus der Widget-Maske abgeleitet.

## 4. Semantische Source-Struktur

Die konkrete Dateiliste wird vor jedem Unterzug gegen den aktuellen Codebestand fixiert. Für den Gesamtzug gelten folgende Grenzen und Namen als Ausgangspunkt:

| Bereich | Source-Grenze | Vorgesehene Namen |
|---|---|---|
| Coprocessor API und Submission | öffentliche Submission- und Result-Semantik | `coprocessor/MRCoprocessor.cpp` |
| Worker-Lebensdauer | Erzeugung, Modulo-Affinität, Loop, Stop, Join, Terminierung | `coprocessor/MRCoprocessorWorkerLifecycle.cpp` |
| Externe Quellen | Registrierung, Quellenstatus, EOF/Close/Cancel | `coprocessor/MRCoprocessorExternalSources.cpp` |
| Instrumentierung | Worker-Events, Snapshots und Messwerte | `coprocessor/MRCoprocessorTelemetry.cpp` |
| Bento-Workspace-Codec | Bento-Layout und Widget-Komposition innerhalb des vorhandenen `WORKSPACE`-Payloads | `app/commands/MRBentoWorkspaceCodec.cpp` |
| Line-Scan | Paketbildung, BOF/EOF und Line-Ledger | `ui/MRFileEditor/MRFileEditorLineWarmup.cpp` |
| Syntax | Checkpoint-Suche, Vorwärtsauswertung und Adoption | `ui/MRFileEditor/MRFileEditorSyntaxWarmup.cpp` |
| Folding | Struktur-Checkpoints, Paketvalidierung und Adoption | `ui/MRFileEditor/MRFileEditorFoldWarmup.cpp` |
| Minimap | Fensterprojektion und Warm-up-Pakete | `ui/MRFileEditor/MRMiniMap.cpp`, `ui/MRFileEditor/MRMiniMapOverlay.cpp` |
| Hex-Panes | Snapshot-Projektion und pane-spezifische Adoption | `ui/MRBentoHexEditor/panes/MRHexPaneProjection.cpp` |

Keine dieser Dateien begründet eine generische Worker-, Registry- oder Task-Framework-Schicht. Neue Typen werden nur eingeführt, wenn sie einen stabilen fachlichen Lebenszyklus ausdrücken und vor dem jeweiligen Unterzug benannt sind.

## 5. Gesamtzug und Prüfziele

| Unterzug | Inhalt | Prüfziel | Status |
|---|---|---|---|
| 0 | Vollständige Bestandsaufnahme aller blockierenden Scan-, Warm-up-, Derived- und Pane-Pfade; Baseline-Messungen | Matrix aus Owner, Lane, Task, Thread, Blocking-Stelle und aktueller Core-Nutzung; reproduzierbare Ausgangsmessung | abgeschlossen; Istzustand nicht bestanden |
| 1 | Worker-Telemetrie und Redesign des Performance Panel | Erzeugung, `workerOrdinal`, Core, Owner, Lane, Task, Generation, Paket, Zustandswechsel und Terminierung live und im Messprotokoll sichtbar | abgeschlossen; Lifecycle, Owner- und Paketmetadaten nachgewiesen; Panel abgenommen |
| 2 | C++18-Worker-Substrat, minimales Identitätsmodell und strikte Modulo-Zuordnung | Redundante Worker-Bezeichner konsolidiert; Owner, Auftrag, Ausführung und OS-Thread eindeutig getrennt; mehr Worker als Cores nachgewiesen; lückenlose Round-Robin-Folge; keine Lane-Serialisierung; sauberer Stop/Join | abgeschlossen; Substrat, Modulo-Zuordnung und Stop/Join nachgewiesen |
| 3 | Line-Scan mit BOF-/EOF-Paketen und Span-Ledger | Großer Cursor-Sprung erzeugt neue Richtungsworker; alte Pakete schließen ab; Ledger verhindert Doppelarbeit | abgeschlossen; Sichtprüfung und technische Abschlussprüfung bestanden |
| 4 | Syntax-Checkpoint-Ketten und geordnete Adoption | Parallelität ohne rückwärts erfundene Syntaxzustände; identisches Ergebnis zur kanonischen Vorwärtsauswertung | abgeschlossen; Adoption, Churn und Interaktionsverhalten sichtgeprüft |
| 5 | Folding-Checkpoint-Ketten und geordnete Strukturadoption | Parallel akquirierte Pakete ergeben dieselben Fold-Spans wie die kanonische Vorwärtsauswertung | abgeschlossen; Lifecycle, Ebenensemantik und kanonischer Fernsprunganker sichtgeprüft |
| 6 | Minimap pro Editorfenster | Drei Fenster wärmen gleichzeitig; kein Fokus-Gate; separate Worker und messbarer Fortschritt je Fenster | abgeschlossen; Churn-Korrektur und Queue-Verhalten maintainer-seitig abgenommen |
| 7 | Bento- und HexBento-Worker | Jedes Pane besitzt sichtbare Worker-Aktivität; keine teure Hex-Projektion in `draw()`; Hintergrund-Panes arbeiten weiter | abgeschlossen; Bento 1.5, Bento-Derived-Projektionen und HexBento mehrfach sichtgeprüft und abgenommen |
| 8 | Externe Quellen, Prozesskanäle, MRMAC Jobs und übrige Blocking-Pfade | Quellen- und Session-Lebenszyklen getrennt; endliche Jobs terminieren; externe Quellen schließen sichtbar; kein unbegründeter Single-Worker-Pfad | abgeschlossen im freigegebenen Korrekturscope; `NAV-001`, `LINE-002`, `DEAD-001`, `EXT-001` und `FCMP-001` abgenommen; MFS und pauschales Datei-/Dialog-I/O sind keine nachträglich implizierten Unterzüge |
| 9 | Workspace Restore und Lifecycle-Übergänge | Owner werden korrekt rekonstruiert; keine restaurierten IDs, verwaisten Worker, verlorenen Ledger oder Fokusabhängigkeiten | abgeschlossen; Restore- und Owner-Neuerzeugung sichtgeprüft |
| 10 | Vergleichsmessung und Abschlussprüfung | UI-Latenz, Durchsatz, Core-Verteilung, Worker-Churn, Speicher und Result-Backlog gegenüber Baseline protokolliert | abgeschlossen; konsolidierte Bewertung in Abschnitt 5.17 |

Jeder Unterzug endet mit einem eigenen technischen Prüfbericht. Ein nachfolgender Unterzug beginnt erst nach der Bewertung des vorherigen Prüfziels.

### 5.1 Prüfbericht Unterzug 0

Unterzug 0 hat keinen Laufzeitcode geändert. Er kombiniert eine vollständige statische Pfadinventur mit reproduzierbaren Prozess-, Thread- und Affinitätsmessungen des experimentellen Branches. Der geprüfte Stand war:

| Merkmal | Wert |
|---|---|
| Datum | 2026-07-19 |
| Branch | `fix/minimap-background-warmup` |
| Commit-Basis | `a7e6accb` zuzüglich des vorhandenen experimentellen Arbeitsbaums |
| System | Linux 7.1.3, x86-64 |
| Compiler | Clang 22.1.8 |
| online / für den normalen Lauf erlaubte CPUs | 16 / 16, IDs 0 bis 15 |
| Affinitätsprobe | Prozess auf CPUs 0 bis 3 beschränkt |

Prüfentscheidung: **Der Istzustand besteht das Prüfziel des Gesamtzugs nicht.** Gewöhnliche `submit()`-Aufträge besitzen auf diesem Branch zwar bereits einen eigenen One-shot-Worker. Syntax und Minimap verwenden jedoch weiterhin je einen persistenten seriellen Owner-Worker; Line-, Fold- und Minimap-Warm-up entsprechen weder der BOF-/EOF-Paketmechanik noch dem geforderten Span-Ledger. Mehrere unbeschränkte Analysen und Projektionen verbleiben im UI-Thread. Worker-Lifecycle, Owner, Generation, Richtung, Paketspan und tatsächlicher Core sind nicht korrelierbar instrumentiert.

### 5.2 Reproduzierbare Baseline

Build des Prüfprogramms:

```sh
make regression-probe CXX=clang++
```

Der Build war erfolgreich und meldete keine Compilerwarnung. Die Kernprüfung wurde mit folgendem Befehl gemessen:

```sh
/usr/bin/time -v ./regression/mr-regression-checks --core
```

Ergebnis: 24 Tests bestanden, ein Test schlug fehl. Der bestehende Fehler lautet:

```text
File compare next-diff compare cursor line mismatch after first jump:
expected 1, got 2.
```

Der erste Messlauf benötigte 1,72 s Wall Time, 1,20 s User Time und 0,36 s System Time bei maximal 50.304 KiB RSS. Drei unmittelbar wiederholte Läufe ergaben:

| Lauf | Wall | User | System | max. RSS | freiwillige / unfreiwillige Context Switches | Status |
|---|---:|---:|---:|---:|---:|---|
| 1 | 2,31 s | 1,21 s | 0,37 s | 49.480 KiB | 1.227 / 1.542 | 1 |
| 2 | 4,05 s | 1,36 s | 0,52 s | 50.240 KiB | 1.738 / 1.518 | 1 |
| 3 | 3,00 s | 1,51 s | 0,57 s | 49.804 KiB | 1.793 / 1.673 | 1 |

Diese Werte sind eine Prozessbaseline des vorhandenen Kernprüfprogramms, keine UI-Latenzmessung. Die hohe Wall-Time-Streuung und die vorhandene Regression verbieten eine Leistungsfreigabe auf ihrer Grundlage.

Die Affinitätsprobe wurde reproduzierbar mit `taskset -c 0-3` und `strace -f -e trace=clone,clone3,sched_setaffinity` gefahren. Sie ergab:

| Messwert | Ergebnis |
|---|---:|
| `clone3()`-Einträge | 964 |
| `clone()`-Einträge | 2 |
| `sched_setaffinity()`-Einträge | 962 |
| erfolgreiche Zielmasken CPU 0 / 1 / 2 / 3 | 240 / 240 / 239 / 238 |
| leere Zielmasken mit `EINVAL` | 5 |
| maximal gleichzeitig beobachtete Prozess-Threads ohne `strace` | 30 |

Die nahezu gleiche Verteilung belegt, dass die im Code vorhandene Modulo-Auswahl grundsätzlich wirksam wird. Sie ist dennoch kein bestandener Modulo-Nachweis:

1. `workerOrdinal`, Worker-ID und Owner erscheinen nicht im Trace und lassen sich deshalb nicht mit dem Systemaufruf korrelieren.
2. Parallel startende Threads erreichen den Systemaufruf nicht zwingend in Erzeugungsreihenfolge. Die beobachtete Aufrufreihenfolge kann daher die mathematische Ordinalfolge weder beweisen noch widerlegen.
3. Fünf Affinitätsaufrufe scheiterten nachweislich mit einer leeren Maske und `EINVAL`.
4. `bindCurrentThreadToModuloCore()` verwirft den Rückgabewert von `pthread_setaffinity_np()`. Fehler werden weder gespeichert noch angezeigt.
5. Die `clone`-Zahlen enthalten auch vom Prüfprogramm erzeugte Prozesse und sind ohne Lifecycle-Telemetrie keine exakte Worker-Anzahl.

Nach ausdrücklicher Installationsfreigabe wurden `perf` 7.1.3-1 und dessen Abhängigkeit `libpfm` installiert. Die Kernel-Einstellung blieb unverändert bei `kernel.perf_event_paranoid=2`. Folgende User-Space-Counter waren ohne weitere Systemänderung zugänglich:

```sh
perf stat -x ';' \
  -e task-clock,context-switches,cpu-migrations,cycles,instructions,branches,branch-misses,cache-references,cache-misses \
  ./regression/mr-regression-checks --core
```

| Lauf | Task Clock | Cycles | Instructions | Branches | Branch Misses | Cache References | Cache Misses |
|---|---:|---:|---:|---:|---:|---:|---:|
| 1 | 1.241,13 ms | 3.272.257.215 | 5.331.251.178 | 1.018.195.054 | 12.122.190 | 95.098.440 | 7.446.189 |
| 2 | 1.274,32 ms | 3.339.066.478 | 5.333.373.241 | 1.018.524.747 | 12.173.677 | 94.208.613 | 7.655.207 |
| 3 | 1.254,07 ms | 3.298.713.610 | 5.338.084.079 | 1.019.705.614 | 12.212.472 | 95.379.654 | 7.418.267 |

Die abgeleitete IPC liegt zwischen 1,60 und 1,63, die Branch-Miss-Rate bei rund 1,19 Prozent und die Cache-Miss-Rate zwischen 7,78 und 8,13 Prozent. Unter `perf_event_paranoid=2` wurden `context-switches:u` und `cpu-migrations:u` mit null ausgewiesen, weil die erforderlichen Kernel-Anteile ausgeschlossen sind. Für Context Switches bleibt deshalb die `/usr/bin/time`-Baseline maßgeblich. Eine Absenkung des Sicherheitsparameters war für die verfügbaren Hardware-Counter nicht erforderlich und wurde nicht vorgenommen.

### 5.3 Bestandsmatrix

Die Bestandsmatrix beschreibt den Zustand vor Unterzug 2. Die Spalte **Owner** benennt darin den damaligen fachlichen Ist-Owner; das inzwischen konsolidierte Owner-Paar war zu diesem Zeitpunkt noch nicht durchgängig vorhanden.

| Bereich | Owner / Lane / Task | aktueller Thread- und Paketpfad | aktueller Ledger | festgestellte Abweichung | Ziel-Unterzug |
|---|---|---|---|---|---:|
| Coprocessor `submit()` | aufrufender Kontext / angegebene Lane / Task-Art | pro Aufruf neuer One-shot-`std::jthread`; ein Auftrag, danach Retirement | keiner im Scheduler | unabhängige Aufträge warten nicht lane-weit, erzeugen aber starken Thread-Churn; Retirement wird erst bei `pump()`/`pumpFor()` geerntet | 1, 2 |
| Coprocessor `submitCoalesced()` | wie `submit()` | erzeugt vor der Einreihung einen neuen, leeren `LaneState` | keiner | Coalescing kann keine ältere Queue desselben fachlichen Schlüssels erreichen und ist für One-shot-Aufträge faktisch wirkungslos | 2 |
| persistenter Worker | Syntax- oder Minimap-Renderer / Compute oder MiniMap | `registerWorker()` erzeugt genau einen Thread; alle Owner-Aufträge warten seriell in dessen Queue | keiner im Scheduler | ein alter Auftrag kann die neue Cursor-Generation desselben Owners aufhalten | 2, 4, 6 |
| Line-Index | Editor/Dokument / Compute / `LineIndexWarmup` | je Chunk ein One-shot-Worker; jeder Chunk erweitert den Index um höchstens zwei Strides | ausschließlich monotoner BOF-Präfix aus Checkpoints, `lazyIndexedOffset` und `lazyIndexedLine` | keine Cursor-Vicinity, keine BOF-/EOF-Richtungsworker, kein Span-Ledger; `lineCount()` kann die Vollauswertung synchron erzwingen | 3 |
| Editor-Zeichenpfad | Editorfenster / UI / keine Task | `MRFileEditor::draw()` ruft trotz zunächst geschätzter Geometrie später unbedingtes `mBufferModel.lineCount()` auf | Line-Präfix | der erste Draw eines großen Dokuments kann den vollständigen Line-Scan auf dem UI-Thread erzwingen; `updateMetrics()` besitzt denselben Vollzählpfad | 3 |
| Syntax-Warm-up | Editorfenster / Compute / `SyntaxWarmup` | ein persistenter Worker je Editor; ein zusammenhängender Vorwärtsauftrag; alter Auftrag wird storniert, neuer wartet in derselben Queue | `validRanges`, Token-Cache und Syntax-Checkpoints | nur EOF-seitiges Prefetch-Ziel; kein gleichzeitiges BOF-/EOF-Paar, keine Generationen, keine geordnete Kette mehrerer Worker; Adoption iteriert unbudgetiert über das gesamte Ergebnis | 4 |
| Folding | Editorfenster / Compute / `FoldWarmup` | ein One-shot-Worker für das gesamte Scanfenster; intern geplante 256-Zeilen-Chunks werden seriell gesammelt, danach gemeinsam ausgewertet | ein sichtbarer Bereich, kein allgemeiner Fold-Span-Ledger und keine Struktur-Checkpoint-Kette | Paketakquisition nicht parallel; Start am Scanfenster ohne bestätigten Strukturzustand; Storno wird während der Fold-Auswertung nur grob beachtet | 5 |
| Minimap | `MRMiniMapRenderer` eines Editors / MiniMap / `MiniMapWarmup` | ein persistent registrierter Worker pro Renderer; Berechnung nur für das aktuelle Sampling-Fenster; Submission wird aus `draw()` angestoßen | ein Cachefenster, kein Warm-Span-Ledger | verdeckte, nicht erneut gezeichnete Fenster erhalten keinen unabhängigen Fortschritt; Sprünge serialisieren auf einem Worker; alte Generation kann als „noch nützlich“ festgehalten werden | 6 |
| Minimap-Overlay | Editorfenster / UI / keine Task | Auswahl-, Find-, Dirty-, Fehler-, Warnungs- und File-Compare-Bereiche werden bei Cache-Miss im Draw-Pfad durchlaufen und gehasht | Signaturen und aktueller Cache | Aufwand wächst mit Bereichs- und Diff-Menge und ist nicht budgetiert | 6 |
| Save-Normalisierung | Editor / nominell Compute / `SaveNormalizationWarmup` | Payload, Dispatch und Cache-Felder existieren; `scheduleSaveNormalizationWarmupIfNeeded()` submitiert derzeit keinen Auftrag, sondern invalidiert beziehungsweise storniert nur | Throughput-/Options-Cache | Task-Art ist derzeit ein unvollständiger Restpfad, kein Warm-up | 8 |
| Indicator-Blink | Fensterindikator / nominell Compute / `IndicatorBlink` | Payload und Dispatch existieren, aber keine Submission; Blink-Fortschritt läuft direkt über die UI-Timerlogik | Generation je Blink-Kanal | tote Coprocessor-Task-Art; für diese kleine UI-Zustandsänderung ist kein Worker belegt | 8 |
| File Compare, Akquisition | Compare-Bento / Compute / `FileCompare` | vollständige Editor-Snapshots und Zeilensplits entstehen auf dem UI-Thread; nur Myers-Diff läuft im One-shot-Worker | Snapshot-Versionen | unbeschränkte Textkopie und Zeilenteilung vor Submission blockieren den UI-Thread | 7, 8 |
| File Compare, Adoption | Compare-Bento / UI | Hunk-Normalisierung, Change-Gruppen, Line-Kinds, Minimap-Slices und Pane-Refresh laufen vollständig bei Ergebnisadoption | Compare-Caches | Adoption besitzt kein Zeit- oder Mengenbudget; bestehender Navigationstest ist rot | 7, 8 |
| Compilerdiagnostik-Bento | CompilerOutput/Problems-Panes / UI / keine eigene Task | bei Output-Aktualisierung wird der gesamte Output materialisiert, vollständig neu geparst, auf Source-Zeilen abgebildet und das Problems-Pane neu aufgebaut | Vektor aktueller Diagnosen | wiederholte Vollanalyse wachsender Ausgabe auf dem UI-Thread; kein Pane-Owner-Worker | 7 |
| Structure-/Functions-Bento | jeweiliges Outline-Pane / UI nach Fold-Ergebnis | Fold-Snapshot wird vollständig in Pane-Text und Entry-Vektor formatiert und per `replaceTextBuffer()` übernommen | Outline-State pro Rolle | Derived-Formatierung und unbeschränkte Adoption im UI-Thread | 7 |
| HexBento Hex/Decimal/Binary/Octal | jeweiliges Pane / UI | jede sichtbare Zelle wird in `draw()` formatiert | keiner | kein Projection Owner, Worker oder Cache | 7 |
| HexBento Strings | Strings-Pane / UI | `mrHexStringCellAt()` wird für jede sichtbare Zelle aufgerufen und kann je Zelle bis zu 512 Bytes vorwärts und rückwärts prüfen | keiner | teure wiederholte Scans in `draw()` | 7 |
| HexBento Inspector | Inspector-Pane / UI | sämtliche Integer-, Float-, LEB128-, GUID- und Textdarstellungen werden bei jedem Draw neu aufgebaut | nur zuletzt gehaltener Zeilenvektor | teure Derived-Projektion in `draw()` | 7 |
| externer Build-/Shell-Auftrag | Kommunikationsfenster / Io / `ExternalIo` | eigener One-shot-Worker hält `poll()`, `read()` und Prozess-Wait für die Prozesslebensdauer | Task- und Kanal-ID | keine Lane-Serialisierung, aber Quellenlebenszyklus und Worker-Terminierung sind nicht als solche instrumentiert | 8 |
| registrierter Live-Stream | Log-Viewer-Quelle / Extern / `ExternalIo` | eigener persistenter Quellenworker bis EOF, Close, Fehler oder Storno | ExternalSource-State | fachlich zulässige Ausnahme; Stop und Join werden erst über Retirement/Pump vollendet und nicht als Lifecycle gezeigt | 1, 8 |
| Log-Viewer-Vorbereitung | Dialog/Log-Viewer / UI | Journal-/Syslog-Identifier werden vor dem Viewer synchron über Fork, Pipe-Read und `waitpid()` ermittelt | keiner | blockierender Prozesspfad vor Quellenregistrierung | 8 |
| Acquire-Dialog | Dialog / UI | Fork, Pipe-Read und `waitpid()` laufen synchron | keiner | externer Prozess blockiert den UI-Thread | 8 |
| MRMAC reiner Hintergrundjob | Exec Session / Macro / `MacroJob` | endliche VM-Ausführung besitzt einen One-shot-Worker | Session- und Task-ID | grundsätzlich parallel; Lifecycle nicht mit Worker/Core korreliert | 1, 8 |
| MRMAC staged job | Exec Session plus Editor-Owner / Macro / `MacroJob` | Snapshot-Ausführung im One-shot-Worker, anschließender UI-Commit | Session- und Task-ID, Konflikt-Snapshot | Adoption und Owner-Korrelation unvollständig instrumentiert | 1, 8 |
| MRMAC External-I/O-Intrinsics | Exec Session / überwiegend UI | Profile mit `mrefExternalIo` sind weder background-safe noch staged; unter anderem `SUBSHELL`, `SHELL_TO_OS`, Dateioperationen und Teile der Prozessruntime laufen daher im UI-Pfad | logische Exec Session | `read()`, `waitpid()` und Shell-Wait können den UI-Thread blockieren; eine Exec Session ist dabei kein Quellenworker | 8 |
| MRMAC `DELAY` | Exec Session / UI oder Macro | Vordergrund-VM yieldet asynchron; Hintergrund-VM schläft in kurzen Storno-Slices auf ihrem eigenen Worker | Delay-Generation beziehungsweise Session | kein UI-Blocking im freigegebenen Vordergrundpfad; blockierender Schlaf bleibt auf den exklusiven Hintergrundjob begrenzt | 8 |
| Multi-File-Search | Suchdialog/Session / UI | rekursive Verzeichnisakquisition, Datei-Reads und Regex-Matching laufen seriell in `collectMultiFileSession()` | globale Suchsession | potentiell lang laufende Scan- und I/O-Arbeit blockiert die UI; kein Coprocessor-Auftrag | 8 |
| Datei laden/speichern und Block-Arena-I/O | Editorfenster / UI | `readTextFile()`, `ifstream` und `ofstream` werden synchron aus Befehls- und Editorpfaden aufgerufen | Dokument-/Dateistatus | große Dateien und langsame Dateisysteme können den UI-Thread blockieren; atomare UI-Commitgrenze ist noch nicht von I/O getrennt | 8 |
| Workspace Restore | Fenster/Bento-Pane / UI | Workspace serialisiert Layout und fachliche Zustände, aber keine Worker- oder Task-IDs; Editor-Konstruktion registriert neue Minimap-Worker, Syntax-Worker entstehen später lazy | kein persistierter Warm-Ledger | alte Worker-IDs werden korrekt nicht restauriert; eine vollständige Owner-/Worker-Beziehung und Ledger-Neuerzeugung ist mangels Owner-ID und Telemetrie nicht beweisbar | 9 |

### 5.4 Querschnittsbefunde

1. `TaskInfo` enthält Task-ID, Lane, Art, Dokument-ID, Version, Label und Stop-Flag. Execution Owner, Worker-ID, Ordinal, Core, Generation, Richtung und Paketspan fehlen.
2. `WorkerState` hält eine Worker-ID, verliert aber den nur in der Thread-Lambda geführten `workerOrdinal`. Der zugewiesene Core und das Ergebnis der Affinitätssetzung werden nicht gespeichert.
3. Coprocessor-Snapshots aggregieren Worker als Lane-Slots. Ein Slot ist keine stabile Worker-Identität.
4. `mr::performance::Event` enthält Abschluss- und Zeitdaten von Tasks, aber keine Worker-Lifecycle-Ereignisse. Der globale Verlauf ist auf 64 Abschlussereignisse begrenzt.
5. Das Performance Panel zeigt aktive und wartende Task-Slots sowie letzte Laufzeiten. Es zeigt weder Erzeugung, Assignment, OS-Thread, Core, Owner, Generation und Paket noch Stop und Terminierung.
6. Der aktuelle One-shot-Ansatz beseitigt Lane-weites Warten für gewöhnliche Submissions, erzeugt aber sehr viele kurzlebige Threads. Die eifrige Minimap-Worker-Registrierung jedes Editors erzeugt zusätzlich Threads auch für unterdrückte oder nie verwendete Minimap-Pipelines.
7. Syntax besitzt als einziger untersuchter Bereich bereits echte gültige Range- und Checkpoint-Strukturen. Line-Index besitzt nur einen BOF-Präfix; Folding, Minimap und Pane-Projektionen besitzen die geforderten Ledgers noch nicht.

### 5.5 Messlücken nach Unterzug 0

Folgende Ausgangsgrößen lassen sich mit der vorhandenen Instrumentierung nicht glaubhaft erheben und werden deshalb nicht erfunden:

- UI-Thread-Latenz pro Draw, Event und Adoption,
- Fortschritt und Durchsatz getrennt nach Fenster, Pane, Richtung und Generation,
- exakte Worker-Anzahl pro fachlichem Owner,
- mathematische Ordinal-zu-Core-Folge einschließlich fehlgeschlagener Affinität,
- Worker-Erzeugungs-, Paket-, Ergebnis- und Terminierungszeitpunkte,
- Result-Backlog und Adoptionsbudget pro Owner,
- vollständiger Warm-Span-Zustand, weil mehrere fachliche Ledger noch nicht existieren,
- Kernel-seitig aufgelöste Context Switches und CPU-Migrationen pro Worker; `perf_event_paranoid=2` erlaubt derzeit nur die User-Space-Sicht, während `/usr/bin/time` lediglich Prozesssummen liefert.

Diese Messlücken sind das verbindliche Eingangsproblem von Unterzug 1. Eine spätere Vergleichsmessung muss dieselben Kernprüfbefehle wiederholen, darf sich aber nicht auf sie beschränken.

### 5.6 Prüfbericht Unterzug 1

Unterzug 1 hat die Instrumentierungsgrenze additiv auf dem vorhandenen experimentellen Scheduler aufgebaut. Scheduler-, Paket-, Stop- und Join-Semantik wurden nicht bewusst umgeordnet. Die neue semantische Source `coprocessor/MRCoprocessorTelemetry.cpp` enthält Core-Ermittlung, Affinitätsrückmeldung, den begrenzten Lifecycle-Ring und die Telemetrie-Snapshots. Der Ring hält höchstens 4.096 strukturierte Ereignisse und enthält keine Task-Labels oder Payload-Kopien.

Jedes Ereignis enthält nun:

- monotone Sequenz und monotone Zeit,
- die damals noch parallelen Worker-ID und `workerOrdinal`, OS-Thread-ID, geplanten beziehungsweise gesetzten Core und den unverfälschten Affinitäts-Rückgabecode,
- die damalige Laufzeit-Owner-ID, Owner-Art und fachliche lokale ID,
- Lane, Task-Art, Task-ID, Dokument-ID und Dokumentversion,
- vorbereitete Felder für Generation, BOF-/EOF-Richtung und Paketspan,
- Queue-, Lauf-, Result-Warte-, Akzeptanz-, Adoptions- und Gesamtzeit,
- Zustand, Task-Status und Abschlussgrund.

Erfasst werden `created`, `assigned`, `queued`, `running`, `result-ready`, `accepted`, `adopted`, `discarded`, `stopping` und `finished`. Das Leeren noch nicht begonnener Queues erzeugt ein begründetes `discarded`-Ereignis. Die damals noch vorhandene Coalescing-API erzeugte ebenfalls diesen Zustand; sie wurde in Unterzug 2 als wirkungslos entfernt. Unmittelbar anwendbare Ergebnisse wechseln anhand der tatsächlichen Versions- und Apply-Entscheidung direkt nach `adopted` oder `discarded`. Ein zunächst nur von einem fachlichen Ledger übernommenes Ergebnis wechselt nach `accepted`; seine spätere Veröffentlichung oder Ablehnung erzeugt getrennt `adopted` beziehungsweise `discarded`. Externe Chunks ohne Ziel-Fenster und konflikthafte staged Macro-Commits werden als verworfen erfasst.

Das Performance Panel wurde von einer Lane-Blockanimation auf eine kompakte technische Live-Ansicht umgestellt. Vier positionsstabile Ereignisslots zeigen Timestamp, Worker/Owner, Core oder Affinitätsfehler, Lane, Zustand, Task, Dokument/Version, OS-Thread, Queue-/Laufzeit sowie Generation, Richtung und Span. Ändert sich ein bereits sichtbarer Worker, wird ausschließlich der Inhalt seines vorhandenen Slots aktualisiert. Ein Ereignis eines nicht sichtbaren Workers verdrängt den Slot mit dem ältesten Ereigniszeitpunkt; die übrigen Slots werden nicht umsortiert. `finished` bezeichnet dabei nur das Ende des Worker-Threads: Eine nachfolgende fachliche Entscheidung über dessen Resultat aktualisiert denselben Slot noch mit `accepted` und danach `adopted` oder `discarded`. Die Lane-Zeile zählt zusätzlich die unterschiedlichen lebenden Execution Owner je Owner-Art, sodass mehrere Worker desselben Editors nicht als mehrere Editoren erscheinen. Der Kopf zeigt Erzeugung und reguläre Fertigstellung, Result-Aktivität, externe Quellen sowie die bestehenden VM-Hash-/Settings-I/O-Raten. Ein sekundenbezogener Lifecycle-Aggregatstreifen trennt akzeptierte und tatsächlich adoptierte Ergebnisse sowie deren Zeiten; drei Braille-Säulen stellen aktive, wartende und result-bereite Arbeit mit vier vertikalen Subzellen je Zeichen dar. Das Panel bleibt ein gepufferter TVision-View; Zeichnen und Ergebnisadoption verbleiben auf dem UI-Thread.

#### Isolierter Lifecycle- und Modulo-Nachweis

Ein isolierter Coprocessor-Prozess wurde mit `taskset -c 0-1` auf zwei CPUs begrenzt. Er registrierte sieben persistente Worker, submitierte je einen endlichen Auftrag, pumpte alle Ergebnisse, deregistrierte anschließend alle Worker und prüfte den strukturierten Snapshot. Ergebnis:

```text
cores=0,1 workers=7/7 affinity-errors=0
events assigned/queued/running/ready/adopted/stopping/finished=7/7/7/7/7/7/7
```

Für jeden Worker wurde zusätzlich einzeln geprüft:

```text
assignedCore == allowedCoreIds[workerOrdinal % allowedCoreIds.size()]
affinityResult == 0
```

Damit waren die Korrelation der damals parallelen Worker-Bezeichner, des tatsächlichen Cores, der Ergebnisadoption und des Endzustands sowie der Fall „mehr Worker als erlaubte Cores“ für die Instrumentierungsgrenze nachgewiesen. Der in diesem historischen Bericht noch offene C++18-, Identitäts- und Stop-/Join-Nachweis wurde anschließend in Unterzug 2 erbracht.

#### Prozess- und Hardwarezähler nach Instrumentierung

Der auf vier CPUs beschränkte `strace`-Vergleich ergab erneut 962 Coprocessor-Affinitätsaufrufe. Die Zielmasken waren 241/241/240/240 auf CPU 0/1/2/3 verteilt; sämtliche 962 Aufrufe waren erfolgreich. Die fünf leeren Masken mit `EINVAL` aus Unterzug 0 traten in diesem Lauf nicht auf. Der Rückgabewert wird nun in jedem Fall gespeichert und im Panel sichtbar gemacht. Die Ursache des früheren Fehlers gilt durch einen einzelnen fehlerfreien Lauf noch nicht als abschließend beseitigt.

Die unveränderte Kernprüfung wurde dreimal mit denselben `perf stat`-Events wie in der Baseline gemessen:

| Lauf | Task Clock | Cycles | Instructions | Branches | Branch Misses | Cache References | Cache Misses |
|---|---:|---:|---:|---:|---:|---:|---:|
| 1 | 1.224,21 ms | 3.235.908.461 | 5.340.121.976 | 1.019.567.348 | 12.207.669 | 94.313.249 | 7.287.063 |
| 2 | 1.220,13 ms | 3.207.018.463 | 5.335.066.785 | 1.018.536.716 | 12.234.483 | 95.567.583 | 7.720.157 |
| 3 | 1.224,67 ms | 3.186.949.811 | 5.326.692.055 | 1.017.138.290 | 12.161.036 | 95.062.963 | 7.569.156 |

Gegenüber der Unterzug-0-Serie ist in diesem synthetischen Kernlauf keine messbare Verschlechterung erkennbar. Task Clock und Cycles lagen geringfügig niedriger, die Instruktionszahl praktisch unverändert und die Cache-Miss-Zahl innerhalb der bisherigen Streuung. Daraus wird ausdrücklich keine UI-Latenzaussage abgeleitet.

Der vorgeschriebene saubere Build `make clean all CXX=clang++` sowie ein inkrementeller Wiederholungsbuild waren erfolgreich und meldeten keine Compilerwarnung. Die Kernprüfung meldete unverändert 24 bestandene und einen fehlgeschlagenen Test. Der bereits in Unterzug 0 dokumentierte File-Compare-Navigationsfehler ist identisch; es kam kein neuer Testfehler hinzu.

#### Prüfentscheidung und abgegrenzte Lücken

Der technische Lifecycle-Teil von Unterzug 1 ist bestanden: Erzeugung, Core-Assignment samt Fehlercode, Task-Lauf, Ergebnisbereitschaft, Adoption beziehungsweise Verwerfen, Stop und Terminierung sind korreliert sichtbar und messbar. Zwei fachliche Teile des Prüfziels bleiben bewusst offen und werden nicht durch Platzhalter als umgesetzt ausgegeben:

1. Die zu diesem Prüfzeitpunkt vorhandene `executionOwnerId` gehörte zur Laufzeit des jeweiligen `LaneState`-Ausführungscontainers. Unterzug 2 ersetzte sie durch das fachliche Paar `(ExecutionOwnerKind, executionOwnerLocalId)`. Die ownerlokale ID stammt nun aus dem Editor-, Pane-, Session-, Quellen- oder Kanal-Lebenszyklus und ist nicht mehr ein zweiter Worker-Zähler. Die vollständige Rekonstruktion aller Pane-Owner beim Workspace Restore bleibt Prüfziel von Unterzug 9.
2. Generation, Richtung und Paketspan sind im Ereignisschema und Panel vorhanden, bleiben für heutige unpaketierte Aufträge jedoch `-`. Tatsächliche Werte können erst die BOF-/EOF-, Syntax-, Fold-, Minimap- und Pane-Paketbildner liefern. Werte aus Task-Labels zu erraten wäre eine falsche Instrumentierung und wurde deshalb unterlassen.

Unterzug 1 wird daher als technisch implementiert und hinsichtlich Lifecycle/Modulo nachgewiesen bewertet, nicht als Beweis der noch nicht existierenden fachlichen Owner- und Paketarchitektur.

### 5.7 Zwischenzug: paralleler Display-Width-Warm-up

Die bisherige horizontale Metrik enthielt mit `MRFileEditor::longestLineWidth()` einen vollständigen synchronen Zeilendurchlauf auf dem UI-Thread. Dieser Pfad wurde entfernt. Sobald der exakte Line-Index einer Dokumentversion vorliegt, erzeugt jedes Editorfenster eine Display-Width-Generation mit

```text
packetCount = min(indexedLineCount, allowedCoreCount)
```

zusammenhängenden, lückenlosen Zeilenpaketen. Jedes Paket erhält einen eigenen endlichen Compute-Worker. Die Worker-Ordinale laufen durch die bestehende globale Modulo-Zuordnung, der Auftrag trägt `TaskKind::DisplayWidthWarmup`, Generation, Vorwärtsrichtung und den halboffenen Zeilenspan. Ein Worker liest ausschließlich aus einem unveränderlichen Dokument-Snapshot, berechnet die tabulator- und Unicode-abhängige maximale Darstellungsbreite seines Pakets, liefert genau ein Resultat und terminiert.

Der editorlokale Ledger besitzt genau einen Eintrag je Kernpaket und wächst damit mit der Zahl erlaubter Cores, nicht mit der Zahl von Zeilen. Resultate dürfen in beliebiger Reihenfolge eintreffen. Die UI adoptiert ausschließlich passende Dokument-ID, Version, Generation, Task-ID und Paketgrenzen. Während einer unvollständigen Generation kann die veröffentlichte Breite nur wachsen; eine Verkleinerung wird erst nach Adoption sämtlicher Pakete exakt wirksam. Änderungen am Dokument oder an `TAB_SIZE`, `LEFT_MARGIN`, `RIGHT_MARGIN` beziehungsweise `FORMAT_LINE` invalidieren die Generation und setzen neue endliche Worker in Gang. Fokusverlust besitzt keinen Sonderpfad und pausiert die Arbeit nicht.

#### Begriffs- und Datengrenzen

`WIDTH` bezeichnet ausschließlich den Task `DisplayWidthWarmup`. Ein WIDTH-Worker ermittelt nicht eine Zeile aus den Blockdaten, sondern reduziert die visuellen Breiten aller Zeilen seines halboffenen Spans auf ein lokales Maximum. Die Messung zählt Terminalzellen, berücksichtigt die Unicode-Zeichenbreite und expandiert Tabs aus der jeweiligen visuellen Spalte. Das Resultat enthält Generation, Startzeile, Endzeile und maximale Breite; einzelne Zeilen und einzelne Zeilenbreiten werden nicht gespeichert. Die Adoption bildet über alle Paketresultate erneut das Maximum. Direkter fachlicher Verbraucher dieses Ergebnisses sind die horizontale Viewport-Metrik und das Scroll-Limit.

Davon zu trennen ist die Zeilenadressierung. `lineStartByIndex()` übersetzt eine Dokumentzeilennummer über den Line-Index in einen logischen Dokumentoffset. `lineText()` liest anschließend den Zeilentext aus dem unveränderlichen Piece-Table-Snapshot. Diese Zeilendaten besitzen folgende Verbraucher:

| Verbraucher | Verwendung der Zeilendaten |
|---|---|
| Editor-Rendering | Text und Startoffset der sichtbaren Dokumentzeilen |
| Cursor, Navigation und Maus | Abbildung zwischen Dokumentzeile, Offset und visueller Spalte |
| View-State und Fold-Projektion | Wiederherstellung beziehungsweise Abbildung sichtbarer und gefalteter Zeilen |
| Textbearbeitung | Zeilenanfänge für Einfügen, Löschen, Trennen und Verbinden |
| Indenting | Text und Einrückung der aktuellen und benachbarten Zeilen |
| Formatierung | Leerzeilen, Absatzgrenzen und zusammengehörige Zeilenbereiche |
| Syntax | geordneter Zeilentext und vorwärts wirkender Zustandskontext |
| Folding und Outline | Zeilentext und vorwärts wirkender Strukturzustand |
| Minimap | Zeilentext für Belegungsmasken und dokumentbezogene Sampling-Bereiche |
| Blockoperationen | Start- und Endoffsets zeilenweiser und rechteckiger Bereiche |
| Display Width | Zeilentext für die visuelle Breitenreduktion |
| File Compare | Auflösung vergleichbarer Zeilenbereiche |
| Diagnostics und Debugger | Abbildung von Quellzeilen auf Text beziehungsweise Cursoroffsets |

Minimap, Indenting, Syntax und Folding konsumieren damit nicht das WIDTH-Ergebnis. Sie teilen mit WIDTH ausschließlich die darunterliegende Zeilenadressierung und den Zugriff auf Zeilentext. Das wiederholte Technical Manual muss diese Trennung, die Verbraucher und den gemeinsamen sequenziellen Zeilenbereichszugriff ausdrücklich darstellen.

Die Paketmetadaten-Regressionsprüfung weist nach, dass Generation, EOF-Richtung und Span den Worker-Lauf unverändert durchlaufen. Der Kernlauf meldete danach 25 bestandene Prüfungen und weiterhin ausschließlich den bereits dokumentierten File-Compare-Navigationsfehler. Ein interaktiver Lauf mit `misc/enwik9` (954 MiB, 13.147.025 Zeilen) und zwei restaurierten Editoransichten blieb während der Hintergrundlast bedienbar: Das Panel nahm den Öffnungsbefehl an, zeigte im beobachteten Spitzenzustand 184 lebende Worker, davon 180 Compute-Worker, und null Affinitätsfehler; innerhalb des anschließenden Zehn-Sekunden-Fensters fiel die Zahl wieder auf den persistenten Grundbestand. In einem zweiten Lauf wurde ein Paket aus zwanzig `PageDown`-Ereignissen während der anfänglichen Hintergrundarbeit innerhalb des 250-ms-Beobachtungsfensters vollständig bis Zeile 329 umgesetzt. Das ist ein grober Input-Burst-Nachweis, bildet aber weder Wheel-Ereignisse noch die Wiederholungsrate einer gehaltenen Cursortaste ab; die vom Maintainer beobachtete Scroll-Latenz bleibt daher ein offener Messpunkt.

Technische Grenze: Innerhalb einer einzelnen extrem langen Zeile muss die Breite wegen tabulator- und Unicode-abhängigem Vorwärtszustand sequenziell bestimmt werden. Abbruch wird derzeit zwischen Zeilen geprüft; dies blockiert nicht den UI-Thread, kann aber die Terminierung genau dieses Workers bis zum Ende einer außergewöhnlich langen Einzelzeile verzögern. Der Width-Warm-up beginnt außerdem bewusst erst nach dem exakten Line-Index; die Paketbildung erfindet keine Zeilenadressen aus einer Schätzung.

#### Debuggerbefund: wiederholte Zeilenadressierung in enwik9

Ein späterer interaktiver enwik9-Lauf widerlegte die Annahme, dass die Parallelisierung allein bereits eine effiziente WIDTH-Auswertung ergibt. Das Panel zeigte 19 lebende Worker, davon drei untätige persistente Worker und 16 gleichzeitig laufende WIDTH-Paketworker. Der Prozess belegte auf 16 online CPUs etwa 1.480 bis 1.490 Prozent CPU. Der untersuchte Debug-Build war mit `-O0` übersetzt. Der beim Start des Prozesses vorhandene Nice-Wert `-4` wurde auch von den Worker-Threads geerbt; der Projektcode setzt diesen Wert nicht selbst.

Der GDB-Attach zeigte den UI-Thread regulär in der TVision-Ereignisschleife in `select()` und alle 16 WIDTH-Worker in derselben Aufrufkette:

```text
DisplayWidthWarmup
-> lineStartByIndex(lineIndex)
-> localInterpolatedLineStartByIndex(...)
-> nextLine(...)
-> directFindNextLineBreak(...)
```

Zwei Stack-Samples zeigten für den ersten Paketworker einen Fortschritt von Dokumentzeile 498.552 auf 617.618. Ein Deadlock oder eine Endlosschleife liegt daher nicht vor. Die Laufzeit entsteht durch eine ungeeignete Kombination aus wahlfreiem und sequenziellem Zugriff: Die WIDTH-Schleife ruft für jede aufeinanderfolgende Zeile erneut `lineStartByIndex()` auf. Bei vollständigem Lazy-Index beginnt diese Funktion am vorherigen Checkpoint und läuft bis zur angeforderten Zeile vorwärts. Der direkte Checkpoint-Abstand beträgt 4.096 Zeilen. Innerhalb jedes Checkpoint-Blocks werden deshalb näherungsweise

```text
0 + 1 + 2 + ... + 4.095
```

Zeilenübergänge wiederholt. Für 13.147.026 logische Zeilen ergibt das im Mittel 2.047,5 erneut durchlaufene Zeilen pro Abfrage, ungefähr 26,9 Milliarden `nextLine()`-Aufrufe für das Dokument beziehungsweise 1,68 Milliarden je WIDTH-Paketworker. `-O0` vergrößert die beobachtete Laufzeit, ist aber nicht die algorithmische Ursache.

Jeder Worker veröffentlicht erst nach seinem vollständigen Span von ungefähr 821.690 Zeilen ein Resultat. Da alle 16 Pakete nahezu gleich groß sind und gleichzeitig beginnen, bleibt `done:0` minutenlang stehen und die Resultate erscheinen anschließend nahezu gemeinsam. Die Messung belegt damit reale Parallelität, aber zugleich eine algorithmisch aufgeblähte Gesamtarbeit und fehlende sichtbare Fortschrittsauflösung innerhalb eines Pakets.

Der erforderliche Korrekturpfad ist ein linearer Zeilenbereichsscan: `lineStartByIndex(startLine)` wird genau einmal ausgeführt; danach schreitet der Worker mit `nextLine(currentOffset)` bis zum Span-Ende fort. Das reduziert die Zeilenadressierung von näherungsweise `O(N * checkpointStride)` auf `O(N)`, ohne Paketbildung, Worker-Modulo, Generation oder Ledger-Adoption zu ändern. Dieselbe Zugriffsform ist die fachliche Grundlage für sequenzielle Bereiche in WIDTH, Minimap, Syntax, Folding und Formatierung; sie darf jedoch nicht als zweiter Line-Index oder eigener Dokument-Cache implementiert werden.

#### Lineare Korrektur und enwik9-Nachweis

Der WIDTH-Paketlauf adressiert seit der Korrektur nur noch den Anfang seines Spans mit `lineStartByIndex(startLine)`. Innerhalb des Pakets wird der Offset ausschließlich mit `nextLine(currentOffset)` fortgeschrieben. Abbruchprüfung, Breitenreduktion, Payload, Generation, Paketgrenzen und Adoption blieben unverändert.

Ein isolierter interaktiver Lauf in einem realen 80×24-Pseudo-Terminal mit eigenem `XDG_CONFIG_HOME` wurde unter GDB auf die finale enwik9-WIDTH-Generation gefiltert. Die Generation 2 umfasste exakt 13.147.026 logische Zeilen. Ihre 16 disjunkten halboffenen Pakete deckten den Bereich `0..13.147.026` lückenlos ab; jedes Paket umfasste 821.689 oder 821.690 Zeilen. Die erste außer Reihenfolge eintreffende Adoption wurde 5,434 Sekunden nach Prozessstart beobachtet, die letzte nach 6,384 Sekunden. Darin sind 16 Debugger-Breakpoint-Stopps enthalten. Anschließend zeigte das Panel keine weitere CPU-Arbeit: `CPU:0`, `workers:+0/-0`, `done:0`; die zwei verbleibenden untätigen Minimap-Worker gehören zu `MINI-001` und sind kein WIDTH-Restbestand.

Die zuvor beobachtete Laufzeit von mehr als sechs Minuten ist damit nicht durch Queue-Management oder zu viele Worker verursacht worden, sondern durch die wiederholte Zeilenadressierung. `WIDTH-003` ist algorithmisch korrigiert und für den enwik9-Vollscan bestanden. Quantifizierte Wheel- und Held-Cursor-Latenz bleiben als eigenes Interaktionsprüfziel offen.

### 5.8 Prüfbericht Unterzug 2

Unterzug 2 ersetzt das experimentelle C++20-Lebensdauersubstrat durch klassische, explizite C++18-Mechanik. `coprocessor/MRCoprocessorWorkerLifecycle.cpp` enthält als semantische Source ausschließlich Registrierung, Erzeugung, Worker-Loop, Stop, Join und Ernte fertiger Worker. Der bestehende `LaneState` besitzt nun direkt einen `std::thread`, einen atomaren Worker-Stop-Zustand und eine `std::condition_variable`. Der Auftrag besitzt genau einen gemeinsam sichtbaren atomaren Abbruchzustand. `std::jthread` und `std::stop_token` kommen außerhalb der eingebetteten TVision-Fremdquelle nicht mehr vor.

Die Identitäten sind nach Lebensdauer getrennt:

| Bedeutung | kanonisches Feld | Lebensdauer |
|---|---|---|
| fachlicher Owner | `(ExecutionOwnerKind, executionOwnerLocalId)` | Editor, Pane, Session, Quelle oder Kanal |
| Auftrag | `TaskInfo::id` | eine Submission |
| Worker-Ausführung | `workerOrdinal` | Erzeugung bis `Finished` |
| Plattformthread | `osThreadId` | vom Betriebssystem vergebener Thread-Wert |
| Rechenressource | `assignedCore` | stabil für die Worker-Ausführung |

Der redundante `workerId` und die workergebundene `executionOwnerId` samt ihrer Zähler wurden entfernt. Das Performance Panel zeigt entsprechend `W<workerOrdinal>`, das fachliche Owner-Paar, `cpu:<assignedCore>` und `os-tid:<osThreadId>` als unterschiedliche Kategorien. `created` und `finished` bleiben reine Summen.

Jede gewöhnliche `submit()`- oder `submitPacket()`-Submission erzeugt einen eigenen endlichen Worker. Die Lane klassifiziert den Auftrag, enthält aber keine gemeinsame Queue, an der unabhängige One-shot-Aufträge warten. Die Core-Zuordnung wird genau einmal bei der Erzeugung aus

```text
allowedCoreIds[workerOrdinal % allowedCoreIds.size()]
```

berechnet. Es existiert keine Kappung der Workerzahl auf die Zahl der Cores. Die wirkungslose One-shot-API `submitCoalesced()` wurde vollständig entfernt; sie konnte wegen ihres jeweils neu erzeugten `LaneState` keinen älteren Auftrag desselben Schlüssels erreichen.

Persistente Quellen- oder Owner-Worker verwenden dieselbe Ordinal- und Modulo-Mechanik. `unregisterWorker()` setzt den Worker-Stop-Zustand sowie den Abbruchzustand des laufenden und der wartenden Aufträge, weckt den Worker und verschiebt ihn in die Retirement-Liste. `reapRetiredWorkers()` joint ausschließlich bereits als `Finished` markierte Threads und blockiert daher den UI-Thread nicht auf laufender Arbeit. Einziger synchron wartender Join ist `shutdown()` beziehungsweise die Coprocessor-Destruktion: Vor der Freigabe des `LaneState` muss der referenzierende OS-Thread zwingend beendet sein. Dieser Prozessabschluss-Pfad ist eine technisch notwendige Lebensdauerschranke, kein zulässiger interaktiver Arbeits- oder Fokuspfad. Ein normal auslaufender endlicher Worker endet mit `Finished(WorkerFinished)`, ein explizit gestoppter Worker mit `Finished(StopRequested)`.

Der vorhandene Coprocessor-Regressionsharness wurde nach der Sichtprüfung erweitert. Er beschränkt die beim Konstruktor sichtbare CPU-Menge temporär auf höchstens zwei Cores, stellt die ursprüngliche Affinität vor Worker-Erzeugung wieder her und erzeugt anschließend `allowedCoreCount + 3` endliche Worker derselben Compute-Lane. Alle Worker müssen gleichzeitig eine gemeinsame Freigabeschranke erreichen. Damit ist dynamisch ausgeschlossen, dass die Aufträge hinter einem Lane-Worker serialisiert werden. Für jedes `Assigned`-Ereignis werden Ordinal, exakter Modulo-Core, erfolgreicher Affinitätsrückgabecode und eine gültige OS-Thread-ID geprüft.

Danach startet derselbe Harness einen persistenten Worker, wartet bis zu dessen laufendem Auftrag, deregistriert ihn und prüft das abgebrochene Resultat, `Finished(StopRequested)`, ausgeglichene `created`-/`finished`-Summen und einen leeren Live-Worker-Snapshot nach dem Join. Paketgeneration, Richtung und Span des ursprünglichen Metadatentests bleiben zusätzlich geprüft.

Der verschärfte Harness bestand fünf unmittelbar wiederholte Core-Läufe. Jeder Lauf endete insgesamt mit 26 bestandenen und dem unveränderten, fachfremden File-Compare-Navigationsfehler als einzigem fehlgeschlagenen Test. `make regression-probe CXX=clang++` und der vorgeschriebene Abschlussbuild `make clean all CXX=clang++` waren erfolgreich und meldeten keine Warnung. Die MRMAC-v1-Suite einschließlich Staged-Eligibility, Macro-Compile-Sweep und Background-Staged-Probes endete mit `PASS`.

Prüfentscheidung: **Das Substrat-, Identitäts-, Modulo-, Lane-Unabhängigkeits- und Stop-/Join-Prüfziel von Unterzug 2 ist bestanden.** Damit ist noch keine BOF-/EOF-, Syntax-, Folding-, Minimap-, Bento- oder externe Quellenpipeline als fachlich abgeschlossen erklärt. Deren Paketbildung, Ledger und Adoption bleiben ausdrücklich Gegenstand der Unterzüge 3 bis 8.

### 5.9 Implementierungsstand Unterzug 3

Der Line-Scan verwendet rein arithmetisch gebildete Bytefenster von acht MiB. Die UI sucht vor der Submission keine Zeilengrenzen. Pro Editor und aktueller Line-Scan-Generation dürfen bis zu `allowedCoreCount` endliche Compute-Worker gleichzeitig existieren. Diese Zahl ist kein globales Workermaximum: Weitere Editorfenster und noch auslaufende ältere Cursorgenerationen dürfen zusätzliche Worker erzeugen. Sämtliche Worker durchlaufen unverändert die globale Ordinal-Modulo-Zuordnung.

Ein Wechsel des Cursorfokus in ein anderes Acht-MiB-Fenster eröffnet eine neue Generation. Diese Generation reserviert die nächsten noch nicht angewärmten Bereiche abwechselnd in BOF- und EOF-Richtung. Bereits laufende Pakete älterer Generationen werden durch den Cursorsprung nicht abgebrochen; sie beenden ihr endliches Fenster. Nach Dokumentänderung, explizitem Fensterabbau oder Prozessende gilt dagegen die normale Abbruch- und Lebensdauersemantik.

Der dokumentgebundene Ledger unterscheidet drei Mengen:

- das lückenlos von BOF aus aufgelöste Präfix,
- außer Reihenfolge eingetroffene, noch nicht an das Präfix anschließbare Paketresultate,
- aktive Reservierungen laufender Pakete.

Paketplanung betrachtet alle drei Mengen und reserviert ausschließlich unbedeckte Bytebereiche. Dadurch erzeugen weder neuere Cursorgenerationen noch mehrere Ansichten desselben geteilten Dokuments Doppelarbeit. Sobald ein außer Reihenfolge eingetroffenes Paket an das aufgelöste Präfix anschließt, werden seine lokalen Zeilenzahlen in absolute Checkpoints übersetzt. Danach werden unmittelbar alle nun ebenfalls anschließenden Pakete adoptiert. Erst wenn das Präfix EOF erreicht, werden exakte Gesamtzeilenzahl und vollständiger Line-Index veröffentlicht.

Die Worker behalten den vorhandenen SIMD-Pfad bei. Direkte Speicheransichten suchen CR und LF mit SSE2 in 16-Byte-Blöcken; Piece-Table-Chunks werden über denselben vektorisierten Suchkern gespeist. Plattformen ohne SSE2 verwenden den vorhandenen skalaren Ersatzpfad. Jedes Paket speichert nur jeden 4.096. lokalen Zeilenumbruch als sparse Checkpoint sowie seine Gesamtzahl, nicht sämtliche Zeilenstarts. Ein `CRLF`, das eine Paketgrenze schneidet, wird dem Paket mit dem abschließenden `LF` zugerechnet. Damit sind Paketresultate ohne Doppelzählung addierbar.

Das Performance Panel erhält die Metadaten direkt aus `submitPacket()`: `IDX`, Dokument und Version, Generation, `BOF` oder `EOF` sowie den halboffenen Bytespan. Regulärer Abschluss erscheint als `finished`; erfolgreiche UI-Adoption als `adopted`. Der Datei-Informationsdialog und der Fenstertaskmarker zählen sämtliche aktiven Line-Scan-Pakete statt nur einer stellvertretenden Task-ID.

Der interaktive Zeichen- und Cursorpfad erzwingt bei unbekannter Gesamtzeilenzahl keinen vollständigen `lineCount()`-Scan mehr. Darstellung, Scrollgrenzen und Cursormarker verwenden bis zur exakten Adoption eine Schätzung. Das kann die vorläufig angezeigte Zeilennummer verändern, sobald der BOF-Präfix den Cursor erreicht; es hält den vollständigen Dokument-Scan vom UI-Thread fern.

Ein `TextDocument`-Copy übernimmt den bereits geordnet adoptierten Line-Index und dessen Checkpoints, nicht jedoch aktive Paketreservierungen oder noch nicht anschließbare Out-of-order-Ergebnisse. Damit kann weder ein abgetrennter Shared-Buffer noch ein Staging-Dokument auf Worker warten, die ausschließlich der ursprünglichen Dokumentinstanz gehören.

Die erste Sichtprüfung des Workspace Restore legte einen verbliebenen synchronen Umgehungspfad offen: Nach Adoption eines noch unvollständigen Line-Pakets plante der UI-Thread den Syntax-Warmup für eine geschätzte hohe Zeilennummer. `lineStartByIndex()` interpolierte diesen Index mangels anschließbarer Checkpoints durch einen 747.974.285 Byte langen SIMD-Scan im UI-Thread. Ein Live-Debugger-Stack belegte die Kette `after-line-index` → `scheduleSyntaxWarmupIfNeeded()` → `syntaxWarmupLineStarts()` → `lineStartByIndex()` → `piecewiseCountLineBreaksInRange()`. Für große Dokumente wartet der bestehende Syntax-Scheduler nun auf den exakten Line-Ledger. Dies ist eine blockierungsfreie Sicherheitsschranke; die fachliche Parallelisierung der zustandsbehafteten Syntax bleibt ihrem eigenen Unterzug zugeordnet.

Die zweite Sichtprüfung zeigte einen weiteren UI-Fallback. Solange das BOF-Präfix noch nicht bis zum restaurierten enwik9-Viewport reichte, kostete der einmalige `lineStartByIndex()`-Aufruf je Repaint ungefähr 0,5 Sekunden. Anschließend rief `formatSyntaxLine()` für jede sichtbare Zeile erneut `lineIndex(lineStart)` auf; jeder dieser Aufrufe zählte vom letzten absoluten Checkpoint bei Byte 134.032.044 bis zum Viewport bei Byte 694.584.402 und benötigte ungefähr 0,23 Sekunden. Das Laufzeitprotokoll korrelierte das Ende der Blockade exakt mit `complete=1 exact_lines=13147026`. Der Viewport verwendet vor diesem Zeitpunkt nun den Cursor-Byteoffset als lokalen Anker, läuft höchstens 1.024 Zeilen relativ und übergibt den bereits bekannten Zeilenindex an die Formatierung. Exakte Fernadressierung bleibt nach Abschluss des Ledgers unverändert. Inaktive Block-Overlays lösen keine Zeilenindexabfragen mehr aus.

Die anschließende Interaktionsprüfung trennte den verbleibenden Lag von der Line-Scan-Leistung. In zehn Sekunden Cursor-Autorepeat protokollierte das Compilat 42 Fold-, 23 Minimap- und 12 Syntax-Neuplanungen sowie 61 Erweiterungen eines laufenden Syntaxziels; die globale Task-ID stieg um 410. Im selben Intervall überschritten nur zwei `lineStartByIndex()`-Aufrufe zwei Millisekunden, kein `lineIndex()`-Aufruf überschritt diese Schwelle. Ungefaltete vertikale Navigation und Wheel-Bewegung verwenden deshalb nun ausschließlich lokale `prevLine()`-/`nextLine()`-Schritte. Außerdem bleibt ein laufendes Fold-Paket erhalten, wenn sein Scanbereich den verschobenen Viewport bereits vollständig umfasst. Minimap- und Syntax-Paketierung sind dadurch nicht abgeschlossen; deren Neuplanungs- und Adoptionsmechanik bleibt Gegenstand der Unterzüge 4 bis 6.

Ein maintainer-seitig eingeschobener XML-Fold-Befund zeigte einen unabhängig davon fehlerhaften Strukturstack: Inline-Leaf-Tags wie `<title>Sondrio</title>` wurden als öffnende Blöcke registriert, weil der Scanner nur einen führenden Öffnungs- oder Schließtag auswertete. Solche Leaf-Tags verblieben auf dem Stack und verhinderten anschließend das Matching echter `</revision>`- und `</page>`-Abschlüsse. Die erste Korrektur behandelte ausschließlich Tags, deren Öffnung und Abschluss auf derselben Zeile stehen; die Sichtprüfung zeigte zusätzlich reale Abschlüsse hinter Nutztext wie `[[uk:Альтман Роберт]]</text>` in enwik9-Zeile 3.114.392. Der Scanner sucht deshalb namensgenaue Schließtags im gesamten Zeileninhalt und schließt alle dort passend verschachtelten offenen XML-Blöcke.

Die zweite Sichtprüfung belegte einen hiervon getrennten Fenstergrenzenfehler. Der bei Zeile 3.114.233 geöffnete `<page>`-Block wurde am künstlichen Scanende 3.114.350 abgeschlossen, obwohl sein reales `</page>` erst in Zeile 3.114.394 steht. Offene XML-Blöcke werden am Ende eines Workerfensters nun nicht mehr als gültige Spans veröffentlicht. Der XML-Viewportkontext umfasst mindestens 512 Zeilen in beide Richtungen; damit liegt die konkrete Abschlussfolge `</text>`, `</revision>`, `</page>` vollständig im endlichen Workerpaket. Überschreitet eine XML-Struktur auch diesen Kontext, bleibt sie bis zur Sichtbarkeit ihres realen Abschlusses bewusst unfaltbar, statt eine falsche Grenze zu veröffentlichen. Dies ersetzt nicht die in `FOLD-001` geforderte allgemeine Checkpoint- und Validierungskette.

Bei der Adoption werden geschlossene Spans, deren Start im ausgewerteten Bereich liegt, gegen die neu validierten Spans abgeglichen. Ihre Endposition wird aktualisiert; nicht mehr belegte Einträge werden entfernt. Liegt der Cursor nach einer Erweiterung innerhalb des nun korrekt geschlossenen Bereichs, wird er an dessen Start zurückgeführt. Dadurch kann eine frühere Fenstergrenze nicht als dauerhafte Fold-Projektion fortleben.

Die zugehörige Wheel-Blockade lag im UI-Projektionspfad. Bei aktivem Fold berechnete `draw()` für jede Bildschirmzeile erneut `documentLineForVisibleLine()` und `lineStartByIndex()`; die EOF-Prüfung wiederholte eine zweite absolute Schleife. Die Darstellung adressiert nun nur die erste sichtbare Dokumentzeile und iteriert anschließend mit `nextLine()`. Ein geschlossener Span verursacht genau einen lokalen beziehungsweise checkpointgestützten Sprung zu `endLine + 1`. Vertikale Cursor- und Wheel-Bewegungen verwenden dieselbe lokale Vorwärts-/Rückwärtsmechanik und überspringen einen Fold als eine sichtbare Zeile.

Die nach der Sichtabnahme ausgeführte Core-Prüfung deckte außerdem eine zu breite Anwendung der vorläufigen Zeilenschätzung auf. `cachedCursorLineIndex()` verwendete sie auch für einen 17-Byte-Puffer, solange dessen Line-Index noch nicht als exakt markiert war. Dadurch verlor eine Column-Block-Markierung ihre Ankerzeile. Nur der Großdateipfad verwendet nun die Schätzung; kleine Dokumente ermitteln den Cursorzeilenindex unverändert exakt. Der Block-Markierungs-Harness ist damit wieder grün.

Die technische Abschlussprüfung von Unterzug 3 bestand Coprocessor-Paketmetadaten, endliche Same-Lane-Worker, strikte Ordinal-Modulo-Zuordnung, Deferred-Line-Index, Piece-Table-Mutation, Block-Markierung, EOF-Scrollgrenze und Post-EOF-Darstellung. Der Core-Lauf endete mit 26 bestandenen Prüfungen und ausschließlich dem bereits vor Unterzug 0 dokumentierten File-Compare-Navigationsfehler. Der infolge der lokalen Fold-Zeicheniteration veraltete Post-EOF-Strukturguard wurde auf die semantisch gleichwertige neue Einmalprüfung aktualisiert. Es wurde keine neue Regression-Infrastruktur angelegt.

Implementiert; der vorgeschriebene saubere Build `make clean all CXX=clang++` wurde ohne Warnungen abgeschlossen. Die maintainer-seitige Sichtprüfung bestätigte anschließend den instantanen Workspace Restore sowie responsives Cursor-Autorepeat und Wheel-Scrolling in enwik9. Regressionchecks wurden vor dieser Sichtprüfung nicht ausgeführt; die technische Abschlussprüfung folgt erst nach dieser Abnahme.

### 5.10 Implementierungsstand Unterzug 4

Der persistente Syntax-Worker je Editor wurde entfernt. Eine Syntaxgeneration erzeugt nun bis zu `allowedCoreCount` endliche One-shot-Pakete über `submitPacket()`. Jedes Paket trägt den Editor-Owner, Dokument und Version, Generation, BOF-/EOF-Richtung sowie einen halboffenen Zeilenspan. Die vorhandene globale `workerOrdinal`-Folge übernimmt unverändert die strikte Modulo-Verteilung auf die erlaubten Cores. Fensterwechsel erzeugen keine gemeinsame Queue und kein Fokus-Gate; jeder Editor verwaltet seine Syntaxpakete unabhängig.

Für zustandsbehaftete Sprachen bezeichnet BOF die fachliche Kontextbeschaffung vor dem Viewport, nicht eine rückwärts laufende Syntaxauswertung. Die Auswertung innerhalb jedes Pakets bleibt kanonisch vorwärts. Der erste Bereich beginnt am nächsten bestätigten Syntax-Checkpoint beziehungsweise an BOF. Weitere Pakete dürfen von einem sprachspezifischen Normalzustand aus vorläufig berechnet werden. Ihr Ergebnis verbleibt im editorlokalen Ledger, bis `stateIn` durch einen bestätigten Vorgängercheckpoint belegt ist. Stimmt der vorbereitete Zustand nicht überein, wird ausschließlich dieses endliche Paket mit dem bestätigten Zustand erneut ausgeführt. Ein spekulatives Resultat wird niemals als sichtbarer Token-Cache veröffentlicht.

Ein bestätigtes Paket veröffentlicht seinen Start, sein Ende und sparse Checkpoints im Abstand von 4.096 Zeilen. Tokenläufe werden nur für den angeforderten Viewport- und Prefetchbereich im Resultat gehalten; reine Kontextpakete transportieren keine Millionen darstellungsbezogener Cacheeinträge. Dadurch bleibt die UI-Adoption proportional zur Zahl der sparse Checkpoints und der tatsächlich angeforderten Darstellungszeilen. Zustandsunabhängige Sprachen dürfen ihre Pakete unmittelbar außer Reihenfolge adoptieren.

Workerabschluss, Ergebnisübernahme und fachliche Veröffentlichung sind drei verschiedene Zeitpunkte. `result-ready` bedeutet, dass der Worker sein Resultat an den UI-Dispatch übergeben hat. `accepted` bedeutet ausschließlich, dass der zuständige Editor das versionsgleiche Resultat in seinen Syntax-Ledger übernommen hat; Task-, Worker-, Generation-, Richtungs- und Spanidentität bleiben hierfür in einem kompakten Lifecycle-Ticket erhalten. Erst wenn `stateIn` gegen den bestätigten Vorgängercheckpoint geprüft und Checkpoints sowie sichtbare Tokenläufe veröffentlicht wurden, folgt `adopted`. Scheitert diese Zustandsprüfung, folgt auf `accepted` ein begründetes `discarded`; anschließend wird nur das betroffene endliche Paket mit dem bestätigten Zustand erneut geplant.

Die Churn-Prüfung bei kontinuierlicher Navigation zeigte einen davon unabhängigen Planungsfehler. Nach einer kleinen Verschiebung war der neue Viewportzustand bereits aus den adoptierten Tokenläufen bestätigbar; die Planung verwendete dennoch nur den vorherigen sparse Checkpoint als Kontextanker. Drei neu sichtbare Zeilen erzeugten dadurch regelmäßig drei reine Einzeilen-BOF-Pakete und zusätzlich ein kleines EOF-Paket. Diese Worker waren fachlich redundant, nicht Ausdruck nutzbarer Parallelität. Die Planung verwendet nun den durch `syntaxConfirmedStateForLine()` belegten Viewportzustand direkt als Kontextanker. Nur wenn dieser Beweis fehlt, wird weiterhin ab einem älteren Checkpoint mit mehreren BOF-Paketen gearbeitet.

Notwendige Abdeckung und geplantes Prefetch-Ziel sind außerdem getrennt. Der Scheduler fordert weiterhin mindestens den sichtbaren Bereich plus den bisherigen Hintergrundabstand, plant bei dessen Unterschreitung jedoch den doppelten Hintergrundvorrat. Kleine Cursor- und Wheel-Schritte verbrauchen diesen Vorrat ohne neue Generation. Ein großer Sprung außerhalb bestätigter Bereiche behält unverändert sein Budget von bis zu `allowedCoreCount` endlichen Paketen. Das Laufzeitprotokoll nennt für jede neue Syntaxgeneration nun `required` und `target`, sodass die vermiedenen Generationen und die tatsächlich genutzte Paketbreite quantifizierbar sind.

Die Messung mit gehaltenem Cursor-Down und anschließendem Page-Down ergab 140 Syntaxgenerationen mit 2.240 Syntax-Workern. Nach der initialen Kontextbeschaffung entfielen 2.225 Pakete auf die EOF-Richtung; die kleinen Page-Down-Generationen wurden ungeachtet ihrer nur rund 102 neuen Zeilen stets auf 16 Worker mit durchschnittlich 6,1 Zeilen verteilt. Das war kein blockierter oder endlos laufender Scan, sondern zu feine Paketierung. `splitSyntaxRanges()` begrenzt die Workerzahl nun zusätzlich durch eine Mindestkörnung von 16 Zeilen. Ein großer Bereich verwendet weiterhin bis zu `allowedCoreCount` Worker und deren unveränderte globale Modulo-Zuordnung; ein Bereich von 102 Zeilen erzeugt höchstens sieben Pakete. Die Grenze reduziert ausschließlich Lifecycle-Aufwand ohne ausreichend unabhängige Nutzarbeit.

Die Säulenskala des Performance Panels bezeichnet den automatisch skalierten Spitzenwert von aktiven, wartenden oder result-bereiten Tasks im letzten Sekundenfenster und ist keine kumulative Workerzahl. Der Skalenwert verwendet die Farbe der allein skalierungsbestimmenden Säule: Rot für aktiv, Gelb für wartend und Grün für result-bereit; bei Gleichstand bleibt er neutral. Zur eindeutigen Messung zeigt der Kopf nun getrennte kumulative `one-shot`-Erzeugungs- und Abschlusszahlen. Feste Zählerfelder schlüsseln diese Summen nach `TaskKind` und `ExecutionOwnerKind` auf; es wurde keine neue Registry eingeführt. Persistente Minimap- und externe Quellenworker bleiben dadurch von One-shot-Taskzahlen unterscheidbar. Die während der Churn-Messung zusätzlich erzeugten dateibasierten Gesamtstände gehörten ausschließlich zum Messaufbau und wurden nach dem quantitativen Nachweis entfernt.

Neue Viewportgenerationen brechen ältere Pakete derselben Dokumentversion nicht ab. Bereits reservierte Kontext- und Tokenbereiche werden bei der Planung berücksichtigt; ältere Worker beenden ihr endliches Paket und können den Ledger weiterhin erweitern. Dokumentänderung, Sprachwechsel, Fensterabbau oder Versionskonflikt beenden beziehungsweise verwerfen die betroffenen Pakete. Result-Dispatch sucht den exakten taskbesitzenden Editor statt alle Fenster mit derselben Dokument-ID als mögliche Empfänger zu behandeln.

Das Performance Panel erhält die Syntaxmetadaten ohne Ableitung aus Labels: `SYN`, Owner, Generation, `BOF` oder `EOF` und Zeilenspan stammen direkt aus `TaskInfo`. Ereigniszeilen unterscheiden `accepted`, `adopted` und `discarded`; der Sekundenstreifen zählt Akzeptanz und Adoption getrennt und zeigt `q/run/accept/adopt` als getrennte Zeitwerte. Fenstertaskmarker und Shutdown zählen beziehungsweise beenden sämtliche Syntaxpakete statt nur einer stellvertretenden Task-ID. Die Highlighter, Tokenregeln, Foldauswertung, Indentierung und der TVision-Zeichenpfad wurden in diesem Unterzug nicht verändert.

Der vorgeschriebene saubere Clang-Build `make clean all CXX=clang++` des Sichtprüfungsartefakts ist ohne Warnungen durchgelaufen. Maintainer-seitig zu prüfen sind ein großer XML-Sprung, mehrere gleichzeitige Editor-Owner, sichtbare Generation/Richtung/Spans, Weiterarbeit eines Hintergrundfensters sowie responsives Cursor- und Wheel-Verhalten. Vor dieser Sichtabnahme werden keine Regressionchecks ausgeführt.

### 5.11 Implementierungsstand Unterzug 5

Eine Fold-Generation erzeugt bis zu `allowedCoreCount` endliche `FoldWarmup`-Pakete. Jedes Paket trägt den Editor-Owner, Dokument und Version, Generation, BOF-/EOF-Richtung sowie einen halboffenen Zeilenspan direkt in `TaskInfo`. Ein Cursorsprung darf eine neue Generation erzeugen, während endliche Pakete älterer Generationen derselben Dokumentversion auslaufen. Dokumentänderung, Sprachwechsel, Fensterabbau und Pipeline-Abschaltung verwerfen dagegen den vollständigen editorlokalen Fold-Zustand.

Der Fold-Checkpoint enthält den offenen Strukturstack, die beiden unmittelbar vorherigen Zeilen in Original- und normalisierter Form sowie höchstens 80 Zeilen Sprachrückblick für mehrzeilige C-, C++-, JavaScript-, Swift-, Rust-, Go-, Kotlin-, C#- und Shell-Strukturköpfe. Ein Paket liest zusätzlich genau eine Look-ahead-Zeile für Indent-, Markdown- und Make-Entscheidungen; diese Zeile gehört weder zum Paketspan noch zum veröffentlichten Resultat.

Resultate dürfen außer Reihenfolge eintreffen. Sie werden zunächst mit `accepted` in den editorlokalen Ledger übernommen. Eine Veröffentlichung ist nur möglich, wenn für `startLine` ein bestätigter Checkpoint derselben Generation existiert und dessen vollständiger Analysezustand mit `stateIn` des Pakets übereinstimmt. Bei einer Abweichung wird das vorläufige Resultat als `discarded` abgeschlossen und ausschließlich dieses Paket mit dem bestätigten Eintrittszustand erneut ausgeführt. Ein bestätigtes Paket veröffentlicht einen Checkpoint an `endLine`; dadurch kann die zusammenhängende Vorwärtsfront mehrere bereits vorliegende Resultate in einem UI-Durchlauf adoptieren.

Die sichtbare Fold-Projektion wird ausschließlich aus einer lückenlosen Kette bestätigter Segmente gebildet. Sie ersetzt den Viewport erst, wenn die Kette mindestens bis zum angeforderten sichtbaren Ende reicht. Offene Strukturblöcke werden an einer Paket- oder Viewportgrenze nicht als geschlossene Spans synthetisiert. Nur ein Paket, das nachweislich das tatsächliche Dokumentende erreicht, darf die bereits bestehenden sprachspezifischen EOF-Abschlüsse erzeugen. Geschlossene Folds werden erst gegen diese bestätigte Projektion revalidiert.

Der Result-Dispatch sucht den exakten Besitzer einer Fold-Task-ID. Ein Resultat kann daher weder einen gleichartigen Task eines anderen Editors löschen noch in ein anderes Fenster mit derselben Dokument-ID adoptiert werden. Fenstertaskmarker und Shutdown zählen beziehungsweise beenden sämtliche Fold-Pakete. Das Performance Panel erhält `FOLD`, Owner, Generation, Richtung und Span aus der normalen Paket- und Lifecycle-Telemetrie; es wurde kein zusätzlicher Hochfrequenz-Dateilog eingeführt.

Ein Rechtsklick auf eine Fold-Spalte startet nun zusätzlich eine dokumentweite, versionierte Fold-Level-Operation. Die bereits validierten lokalen Spans dieser Ebene bilden eine sofort sichtbare Vorschau; der UI-Thread wartet weder auf den vollständigen Line-Index noch auf eine Dokumentanalyse. Sobald der exakte Line-Index vorliegt, wird der vor dem Viewport liegende Kontext genau einmal in kanonischer Vorwärtsrichtung ausgewertet und in Struktur-Checkpoints zerlegt. Danach arbeitet eine Vorwärtskette ab dem bestätigten Viewportanker weiter, während unabhängige Projektionspakete den Präfix von BOF aus diesen bestätigten Checkpoints parallel übernehmen. Höchstens `allowedCoreCount` endliche FOLD-Pakete sind gleichzeitig materialisiert. Ein Strukturresultat wird nur mit seinem bestätigten `stateIn` adoptiert; offene XML-Blöcke erhalten am tatsächlichen, aber unvollständigen Dokumentende keinen synthetischen Abschluss.

Sobald die lückenlose validierte Projektion den aktuellen Viewport erreicht, baut ein eigener endlicher FOLD-Worker ausschließlich das neue Segment aus gleichstufigen, nicht überlappenden Spans und dessen Präfixsummen. Die UI adoptiert den unveränderlichen Segmentzeiger; frühere Segmente werden weder kopiert noch erneut sortiert. Ein Fernsprung erhöht während der laufenden Welle das Projektionsziel und erhält aus dem lokal validierten Viewport sofort eine vorläufige Projektion. Die kanonischen Segmente ersetzen diese Vorschau fortschreitend. Sichtbare↔dokumentweite Zeilenabbildung, Fold-Sprünge und Zeilenzahl verwenden binäre Suche über die Segmentgrenzen; eine vollständige Map wird weder im Event- noch im Zeichenpfad aufgebaut. Ein erneuter Rechtsklick auf dieselbe Ebene storniert eine noch laufende Welle und öffnet die Ebene sofort. Einzelne global geschlossene Spans können als kleine editorlokale Ausnahme wieder geöffnet werden; beim Öffnen eines Elternspans bleiben seine validierten Nachfahren geschlossen.

`FOLD-002` ergänzt für gewöhnliche Viewportgenerationen einen dokument-, versions- und sprachgebundenen kanonischen Kontextledger mit dem ersten bestätigten Checkpoint an BOF. Unveränderliche Zeilentextakquisitionen arbeiten in festen Paketen zu 65.536 Zeilen bis zum exakten EOF parallel; ihre strukturelle Auswertung wird anschließend genau einmal in Checkpointreihenfolge vorwärts validiert. Eine Fernsprunggeneration veröffentlicht keine Foldmarker aus einem leeren lokalen Strukturzustand. Sie wartet, bis ein bestätigter Checkpoint höchstens eine Paketbreite vor ihrem Scanbeginn liegt, und validiert den verbleibenden Abstand mit einem reinen Kontextpaket bis zum exakten Viewportanker. Erst danach darf die normale Viewportprojektion erscheinen.

Der kanonische Ledger überlebt Cursor- und Viewportgenerationen, wird aber bei Dokumentversion, Sprache, Abschaltung des Foldings oder Fensterabbau vollständig ungültig. Alte endliche Worker werden bei einer Supersession nicht gewaltsam abgebrochen; ihre Adoption-Ownership wird entfernt und jedes später eintreffende Resultat verworfen. Fehlerzustände sind an den konkreten Request gebunden: Der identische Request bleibt bis zum nächsten externen Anforderungszyklus gelatcht, während ein geänderter Request kontrolliert neu planen darf. Ein Cachetreffer kann deshalb nicht mehr von einer älteren Viewportgeneration überschrieben werden.

Die Churn-Grenze der kanonischen Kontextbildung beträgt pro Dokumentversion ungefähr zweimal `ceil(lineCount / 65536)` endliche FOLD-Aufträge: je ein Akquisitions- und ein geordnetes Validierungspaket pro Span. Gleichzeitig sind höchstens `allowedCoreCount` Worker materialisiert. Eine Mutation verwirft den versionsgebundenen Ledger und beginnt diese Kette erneut; dies ist der bewusste Laufzeitpreis für kanonischen Strukturzustand und bleibt bei append-intensiven Großdokumenten ein zu beobachtendes Risiko. Cursorbewegungen innerhalb derselben Version starten dagegen keinen neuen kanonischen Vollaufbau.

Der isolierte enwik9-PTY-Selbsttest sprang aus kaltem Zustand in den Bereich um Zeile 13.000.000. Der Viewport blieb bis zum Eintreffen des kanonischen Ankers markerfrei und zeigte danach die validierten XML-Strukturmarker. Das Performance Panel belegte lückenlose 65.536-Zeilen-Spans, geordnete Adoption bis zum letzten verkürzten EOF-Paket und die vollständige Rückkehr der FOLD-Aktivität auf null; die kanonische Hintergrundkette benötigte in diesem Lauf rund 49 Sekunden. Während der Kette blieb die bereits erreichte Editoransicht bedienbar. Die maintainer-seitige Sichtabnahme bestätigte den kanonischen Fernsprunganker und die wegen des absichtlich unvollständigen enwik9-Endes dort korrekt ausbleibenden Folgemarker. Es wurden keine Regressionchecks ausgeführt.

### 5.12 Implementierungsstand Unterzug 6

Der persistente Minimap-Worker je Editor wurde entfernt. Eine neue Fensterprojektion erzeugt endliche `MiniMapWarmup`-Pakete über `submitPacket()`. Jedes Paket trägt den konkreten Editor-Owner, Dokument und Version, Generation, BOF-/EOF-Richtung sowie den halboffenen Quellzeilenspan. Die Pakete eines Editors verwenden keinen fokusabhängigen Scheduler und keine Lane-weite Queue; die vorhandene globale `workerOrdinal`-Folge verteilt sämtliche Worker weiterhin streng modulo über die erlaubten Cores.

Die Textprojektion arbeitet auf einem editorlokalen, speicherbegrenzten Warmfenster-Ledger. Eine Generation übernimmt bereits bestätigte, geometrisch und versioniert passende Zeilen aus älteren Fenstern und aus noch laufenden Generationen. Exakt übereinstimmende, bereits reservierte Zeilenspans werden nicht erneut beauftragt. Eine neue Akquisition umfasst `max(8, 4 * allowedCoreCount)` sichtbare Minimap-Fenster und wird abhängig von der Bewegungsrichtung asymmetrisch vor dem Cursor positioniert. Sie erzeugt höchstens `allowedCoreCount` endliche Pakete; kleine Scrollschritte innerhalb dieses Bereichs erzeugen keine neue MAP-Generation. Sobald alle Zeilen des aktuell sichtbaren Teilfensters vorliegen, darf dieses Teilfenster unabhängig von der noch laufenden Vorwärmung adoptiert werden. Ältere Generationen derselben Dokumentversion beenden ihre endlichen Pakete und können ihr vollständiges Akquisitionsfenster weiterhin in den Ledger eintragen. Dokument, Version, Geometrie, Sampling-Modus und Quellspan bleiben für jede Übernahme zwingend identisch. Der Ledger hält höchstens `max(4, 2 * allowedCoreCount)` vollständige Fenster.

Die Overlay-Projektion für Suche und Selektion, Dirty-Ranges, Compilerfehler und -warnungen sowie File-Compare-Zustände läuft nicht mehr in `draw()`. Die Derived-State-Quellen werden bei ihrer fachlichen Mutation als unveränderliche, revisionsgebundene Vektoren veröffentlicht. Endliche Komponentenpakete erzeugen daraus ebenfalls unveränderliche Minimap-Projektionen. Bei einer reinen Selektionsänderung wird nur die Such-/Selektionskomponente neu berechnet. `draw()` liest ausschließlich die vollständig adoptierte Text- und Overlay-Projektion; es erzeugt weder Range-Signaturen noch dokumentweite Overlay-Masken.

Der Result-Dispatch sucht den Editor ausschließlich über die exakte Task-ID. Ein Resultat kann daher weder an ein anderes Fenster desselben Dokuments adoptiert noch dessen Taskzustand löschen. Fenster-Taskmarker, Pending-Zähler und Shutdown berücksichtigen alle Text- und Overlay-Pakete. Jede Generation bleibt im Performance Panel durch Owner, Generation, Richtung und Span nachvollziehbar; ein eigener Hochfrequenz-Dateitrace wurde nicht eingeführt.

Die erste maintainer-seitige Sichtprüfung bestätigte Restore einschließlich minimiertem Zustand, mehrere gleichzeitig sichtbare Editor-Owner, korrekte Minimap-/Canvas-Korrelation und die Rückkehr von `live`/`idle` auf null. Sie verwarf jedoch die damalige Paketgranularität: PageDown und Großsprünge erzeugten bis zu ungefähr 10.000 beziehungsweise 12.000 result-bereite Pakete. Die Minimap blieb während des langen FIFO-Nachlaufs auf dem letzten Cachezustand stehen; Wheel-Bewegungen wurden mit ungefähr ein bis zwei Sekunden Latenz sichtbar. Temporäres RAM-Wachstum ist für diese Korrektur ausdrücklich kein primäres Schutzziel; maßgeblich sind Worker-/Result-Churn, Nachlauf und UI-Latenz. Ungebundene fachliche Zustände bleiben unabhängig davon unzulässig.

Die Ursache lag nicht in fehlender Rechenleistung: Jede Viewportgeneration erzeugte bis zu `allowedCoreCount` sehr kleine MAP-Pakete, obwohl deren Spans sich weitgehend überlappten. Fertige Resultate sammelten sich während kontinuierlicher Eingabe schneller als der ausschließlich im Idle-Pfad budgetierte Dispatcher sie übernehmen konnte. Die Korrektur reduziert deshalb zuerst die Erzeugungsrate und erhält die bestehende UI-Adoptionsgrenze: große richtungsgewichtete Akquisitionsfenster, spanexakte Übernahme aus laufenden Generationen, keine Doppelreservierung und vorzeitige Adoption des sichtbaren Teilfensters.

Ein erneuter 80×24-PTY-Lauf mit 16 erlaubten Cores restaurierte enwik9 und mehrere Editor-Owner. Die Geometrieänderung durch das Performance Panel erzeugte für vier sichtbare Editoren eine erwartete Initialwelle von 64 MAP-Paketen. Je 100 unmittelbar zugestellte PageDown- und PageUp-Ereignisse blieben innerhalb wiederverwendbarer Akquisitionsbereiche; das Panel zeigte während der Nacharbeit `MAP:0`, während die verbleibenden Ereignisse überwiegend SYN und FOLD zuzuordnen waren. Ein Sprung auf Zeile 13.100.000 erzeugte eine neue endliche MAP-Welle von höchstens 16 Paketen; die Minimap-Projektion wurde während dieser Welle sichtbar und `MAP` kehrte auf null zurück. Der anschließend länger laufende Nachlauf bestand aus Syntax-, WIDTH- und Folding-Arbeit und ist nicht der frühere MAP-Resultstau. Eine globale Adoption aus dem TVision-Eventpfad wurde deshalb nicht eingeführt. Die visuelle Latenzbewertung dieses Korrekturstands blieb bis zur folgenden maintainer-seitigen Sichtprüfung offen.

Die anschließende maintainer-seitige Sichtprüfung bewertet den Korrekturstand als behoben und durchgehend sehr performant. Besonders bestätigt wurde das nun erwartungsgemäße Queue-Verhalten: Die Queue nimmt endliche laufende Wellen sichtbar auf und leert sie zeitnah, statt durch fortlaufend neu erzeugte, überlappende Viewportpakete einen langen Resultnachlauf aufzubauen. Damit sind Interaktionslatenz, MAP-Churn und Queue-Nutzung für das Prüfziel von Unterzug 6 abgenommen.

Der vorgeschriebene saubere Build `make clean all CXX=clang++` wurde nach der Churn-Korrektur erneut vollständig und ohne Compilerwarnungen abgeschlossen. Vor der erneuten Sichtabnahme wurden keine Regressionchecks ausgeführt.

### 5.13 Implementierungsstand Bento 1.5

`MRBentoPaneSpec` und jeder persistierbare Bento-Leaf tragen eine kompakte Widget-Maske. Die beiden bekannten Bits bezeichnen Fold-Gutter und Minimap. Rollenspezifische Konstruktoren erzeugen dieselbe Minimap-Belegung wie vor Bento 1.5; dadurch ändert die Vorbereitung weder Standardlayout noch Chrome. Der Restore übernimmt die gespeicherte Maske in die konkrete Pane-Spezifikation und wendet das Minimap-Bit über den vorhandenen Suppression-Pfad an. Das Fold-Gutter-Bit ist zunächst eine deklarierte Pane-Komposition und greift nicht in die geschützte Folding-Pipeline ein.

Der in `MRBentoWorkspaceCodec.cpp` abgegrenzte Workspace-Encoder schreibt `bento=v1.5` und fügt die Maske als viertes Leaf-Feld an. Der Parser akzeptiert sowohl `v1.5` als auch das bisherige `v1`; bei `v1` werden die bisherigen Widget-Standardwerte aus der Pane-Rolle rekonstruiert. Die Persistenz bleibt vollständig im vorhandenen `MRSETUP('WORKSPACE', ...)`-Pfad. Es gibt keinen zusätzlichen Writer, keinen Settings-Runtime-Schatten und keine Serialisierung abgeleiteter Daten.

Diese Vorbereitung erzeugt keine Worker, Resultate oder Adoptionsarbeit und verändert daher weder Worker-Churn noch Queue-Last. Ihr Risiko liegt ausschließlich in der Workspace-Kompatibilität. Vor weiteren Teilen von Unterzug 7 werden deshalb der Restore eines bestehenden `v1`-Workspaces sowie Save/Restore von normalem Bento und HexBento im neuen Format sichtbar geprüft. Regressionchecks werden erst nach dieser Sichtabnahme betrachtet.

### 5.14 Implementierungsstand Unterzug 7B

Die sechs HexBento-Panes Hex, Strings, Inspector, Decimal, Binary und Octal besitzen jeweils einen eigenen `HexPane`-Execution-Owner. Als ownerlokale ID dient die bereits vorhandene prozesslokale `bufferId()` des konkreten Pane-Fensters. Für eine Projektion entsteht ein endlicher `HexPaneProjection`-One-shot-Worker auf der Compute-Lane; dessen Core-Zuordnung folgt unverändert der globalen `workerOrdinal % allowedCoreCount`-Regel. Ein Pane hält höchstens einen aktiven Projektionsauftrag. Änderungen während dieses Auftrags überschreiben lediglich den gewünschten Folgeschlüssel; nach Ergebnisübernahme wird nur die jüngste noch erforderliche Projektion beauftragt.

Jeder Worker liest einen unveränderlichen Dokument-Snapshot und liefert einen unveränderlichen Payload aus vorberechneten Offsetzeilen, fest begrenzten Zellentexten beziehungsweise den 26 Inspector-Zeilen. String-Erkennung, numerische Formatierung und Inspector-Konvertierung liegen damit vollständig außerhalb von `MRHexPaneView::draw()`. Der Zeichenpfad konsumiert ausschließlich den adoptierten Payload sowie den begrenzten Cursor- und Edit-Overlay. Eine Projektion mit unpassender Dokumentversion oder Datenpane-Geometrie wird nicht unter einer neuen Geometrie gezeichnet.

Der vollständige Projektionsschlüssel enthält Dokument-ID, Version und Länge, Pane-Rolle, Cursor-Projektionsrevision und Cursoroffset, Endianness, Record-Länge, Scrollzustand sowie Pane-Geometrie. Die Rechenäquivalenz ist rollenabhängig: Cursor und Endianness invalidieren nur den Inspector; dessen vollständig berechneter 26-Zeilen-Payload bleibt bei reinem Scrollen und Resize verwendbar. Die fünf Datenpanes erzeugen innerhalb eines unveränderten Viewports keinen neuen Worker für eine reine Cursorbewegung. Verlässt der Cursor den Viewport, ändern sich dessen Scrollfelder und die betroffenen Datenpanes erhalten eine neue endliche Projektion. Damit bleibt der vollständige Zustandsbezug prüfbar, ohne sechs fachlich redundante Worker pro Cursor-Tick zu erzeugen.

Resultate werden über Task-ID, Owner-Art und exakte Pane-`bufferId()` zum weiterhin lebenden Pane geroutet. Der Payload muss exakt dem gestarteten Schlüssel entsprechen; die Adoption darf zusätzlich nur bei weiterhin rechenäquivalentem Wunschzustand und identischer Dokumentversion erfolgen. Ein veralteter endlicher Worker wird nicht abgebrochen, sondern darf normal enden; sein Resultat wird verworfen und löst höchstens den zusammengefassten jüngsten Folgeauftrag aus. Ein Fehler des bereits aktuellen Auftrags erzeugt keinen unmittelbaren Retry-Loop. Erst eine neue externe Projektionsanforderung darf denselben Schlüssel erneut beauftragen.

Alle erfolgreichen Dokumentübernahmen laufen durch `MRFileEditor::syncAfterCommittedDocument()` und senden dort ein eigenes TVision-Broadcast. HexBento begrenzt daraufhin seinen Bytecursor auf den neuen Dokumentstand und fordert die erforderlichen Pane-Projektionen an. Damit invalidieren auch Undo, Redo, Search/Replace, Revert, Prettify und ein programmgesteuertes Laden die Hex-Projektionen, ohne Kommando-Einzelpatches, Observer-Registry oder Polling im Zeichenpfad. Eine direkt aus einem Hex-Pane ausgelöste Änderung wird während des Commit-Broadcasts lokal unterdrückt und anschließend mit dem endgültigen Hex-Cursor genau einmal projektiert.

Beim maximierten Restore werden auch die verdeckten Panes zunächst gegen ihre aus dem vollständigen Split-Baum berechnete Normalgeometrie dimensioniert und erst danach verborgen. Sie arbeiten daher nicht mehr mit bedeutungslosen `1×1`-Projektionen vor. Beim Entmaximieren kann eine geometrisch passende Hintergrundprojektion unmittelbar weiterverwendet werden. Die Workspace-Serialisierung, Pane-IDs und Widget-Maske bleiben unverändert.

Die Telemetrie nennt `HEX`, den konkreten `HX:<bufferId>`-Owner, Generation und gelesenen Bytespan. Beim Strings-Pane umfasst dieser Span zusätzlich den tatsächlich verwendeten 512-Byte-Suchhalo. Das erwartete Churn-Maximum einer gleichzeitigen Dokument- oder vollständigen Viewportänderung beträgt sechs aktive Pane-Worker; Cursorbewegung im vorhandenen Viewport erzeugt nur Inspector-Arbeit. Queue- und Result-Backlog müssen in der Sichtprüfung dennoch ausdrücklich bewertet werden. Der vorgeschriebene saubere Clang-Sichtbuild wurde ohne Compilerwarnungen abgeschlossen. Vor dieser Sichtabnahme werden keine Regressionchecks ausgeführt.

Die erste Sichtprüfung bestätigte die sechs Pane-Owner, Endian-Umschaltung, schnellen Workerabbau und Schließen ohne verwaiste Aufträge. Sie verwarf jedoch die voneinander unabhängigen Cursor- und Scrollwahrheiten der Datenpanes, das kurzzeitige Leeren der Canvases während einer Projektionswelle, die Maximize-Hitpriorität an horizontalen Dividern, die Übernahme des versteckten Source-Cursors nach Undo sowie die fehlende schreibbare EOF-Zeile.

Der erste Korrekturstand führte einen gemeinsamen Record-/Spaltenanker ein und richtete ihn an der kleinsten positiven Kapazität aller fünf Datenpanes aus. Außerdem ließ er einen eigenen 450-ms-Timer die gemeinsame Auswahl in allen Datenpanes abwechselnd ein- und auszeichnen. Die zweite Sichtprüfung verwarf dieses Verhalten: Fünf gleichzeitig blinkende Markierungen machten den Fokus unkenntlich. Bei einer Pane-Aktivierung wurde zudem der gewünschte gemeinsame Anker bereits vor der Klickauswertung auf den alten Cursor verschoben, während sichtbar noch die zuletzt adoptierte Projektion gezeichnet werden konnte. Der Hit-Test verwendete damit eine andere Adressbasis als der Canvas.

Der korrigierte Stand behält genau einen kanonischen Bytecursor und einen gemeinsamen Record-/Spaltenanker, trennt aber Markierung und Fokus. Das ausgewählte Byte wird in jeder Darstellung statisch hervorgehoben, sofern es dort sichtbar ist. Ausschließlich das aktive Eingabepane setzt mit `setCursor()` und `showCursor()` den normalen, von TVision verwalteten Cursor. Der eigene Timer, sein editorweiter Blinkzustand und sämtliche paneübergreifenden Cursor-Redraws sind entfernt. Eine reine Cursorbewegung innerhalb des gemeinsamen Viewports verwendet weiterhin die bereits adoptierte Datenprojektion und erzeugt keinen Datenpane-Worker.

Rendern, Cursorprojektion und Mouse-Hit-Test verwenden denselben tatsächlich dargestellten Projektionsschlüssel. Ein Mausklick wird nur akzeptiert, wenn Dokument-ID, Version, Länge und Record-Länge des sichtbaren Payloads noch aktuell sind; leere Zellen und ein Payload einer älteren Dokumentversion sind nicht adressierbar. Vor der Byteauswahl übernimmt der gemeinsame Viewport den sichtbaren Record-/Spaltenanker. Dadurch bleibt das angeklickte Byte auch während einer laufenden Scroll- oder Geometriewelle an seiner Bildschirmposition. Eine reine Mausaktivierung richtet den Viewport nicht vorzeitig am alten Cursor aus. Tastaturbewegung und tastaturbasierter Pane-Wechsel dürfen den gemeinsamen Anker dagegen anhand der Kapazität des aktiven Panes nachführen. Nicht fokussierte, schmalere Panes können eine außerhalb ihrer eigenen Kapazität liegende Auswahl folglich vorübergehend nicht markieren; diese Einschränkung ist notwendig, um zugleich einen gemeinsamen Ursprung und eine unveränderte Mausposition zu erhalten.

Ein einmaliger Hook nach dem vollständig berechneten Bento-Layout invalidiert die aktive Kapazitätsentscheidung und fordert danach genau eine gemeinsame Projektionswelle an; einzelne `changeBounds()`-Aufrufe erzeugen keine Zwischenwellen. Manuelles Scrollen verändert den gemeinsamen Anker ohne Rücksprung zum Cursor. Bei einer Geometrie- oder Scrollwelle bleibt die letzte Projektion ausschließlich unter ihrem eigenen Schlüssel sichtbar, solange Dokument-ID, Version und Länge unverändert sind; nach einer Dokumentmutation wird kein alter Payload weitergezeichnet. Am Datei- beziehungsweise Record-Ende darf freie Canvasfläche verbleiben.

Undo, Redo und andere Dokumentcommits begrenzen den kanonischen Hex-Cursor nur auf die neue Dokumentlänge. Nur explizite Load-, Search- und Restore-Synchronisation darf den versteckten Source-Cursor in den Hex-Cursor importieren. Der Pane-Chrome-Hit-Test erhält Vorrang vor dem Divider-Drag, sodass auch die unteren Decimal-, Binary- und Octal-Panes maximierbar sind. Die Datenprojektion enthält nach dem letzten Datenrecord genau einen separaten Append-Record mit genau einer adressierbaren Zelle bei Offset `length`; dies gilt auch für ein leeres Dokument, einen partiellen letzten Record und eine exakt recordlange Datei. Der Hex-Schreibpfad verwendet für Offset, Ende und Länge durchgehend `size_t`; die übrigen historischen Aufrufer der generischen Editierschnittstelle bleiben davon unberührt.

Die dritte Sichtprüfung verwarf den Stand erneut. Maus und Pfeiltasten änderten den kanonischen Bytecursor, und auch ein EOF-Commit mutierte das Dokument korrekt; Markierung, nativer Cursor und die neue Append-Zeile wurden jedoch erst nach einem Wheel-Ereignis sichtbar. Ursache war kein Event-Routing und kein Hit-Test, sondern der gepufferte `MRHexPaneWindow`-Canvas. Der zuvor entfernte explizite `MRHexPaneView::drawView()`-Aufruf ließ `TGroup::draw()` bei vorhandenem Buffer nur dessen alten Inhalt wiedergeben. Eine spätere Worker-Adoption zeichnete das Child-View direkt und machte den bereits geänderten Zustand nachträglich sichtbar.

`MRHexPaneWindow::draw()` zeichnet das dedizierte Hex-Child-View deshalb wieder genau einmal vor der Ausgabe des Pane-Puffers. Der Child-Draw aktualisiert den vorhandenen Group-Buffer; `TWindow::draw()` gibt anschließend diesen aktuellen Buffer aus. Es wurde weder ein neuer Invalidierungspfad noch ein weiterer Cursor-Refresh eingeführt. Der bestehende Bento-Content-Flush erreicht damit wieder Markierung und nativen Cursor aller sichtbaren Hex-Panes.

Der verpflichtende isolierte Selbsttest verwendete reale SGR-Mouse- und Tastaturereignisse. In einem 180×40-PTY wurden HEX, Strings, Decimal, Binary und Octal nacheinander angeklickt. Jede Auswahl erschien ohne Wheel sofort in allen Datenpanes; nur das angeklickte Pane erhielt den nativen TVision-Cursor. Eine anschließende Pfeiltaste bewegte Markierungen und Fokus-Cursor ebenfalls unmittelbar. Ein getrennter Test mit einer temporären 6-Byte-Datei sprang auf EOF, zeigte die Append-Zelle bei Offset 6, stellte die Eingabe `41` unmittelbar dar und zeigte nach Commit im selben Lauf die Dokumentlänge 7, das neue Byte sowie die neue Append-Zelle bei Offset 7. Nach dem vollständigen Clean-Build wurde derselbe Kernpfad am finalen Binary nochmals ohne Debugger bestanden. Die temporären Datei- und Konfigurationsbestände wurden anschließend entfernt.

Der vierte Korrekturstand entfernt die verbliebene pane- und fensterweite Neuzeichnung aus der stabilen Interaktion. Hex-Panes deklarieren ihre Inhaltsprojektion als lokal. Ein generischer Bento-Refresh zeichnet für sie nur Pane-Chrome und Scrollbars, nicht erneut sämtliche Child-Canvases. Eine Cursoränderung im vorhandenen Viewport zeichnet in jedem Datenpane ausschließlich die frühere und die neue Recordzeile; eine Fokusänderung aktualisiert nur diese begrenzten Markierungen, den nativen Cursor und den Pane-Rahmen. Ein versionsgleiches Resultat mit unverändertem Viewport wird zeilenweise gegen den zuvor adoptierten immutable Payload verglichen und schreibt nur abweichende Zeilen. Ein vollständiger Pane-Draw bleibt auf Expose, Layout, Maximize/Restore und Resize begrenzt.

Tab und Shift-Tab verwenden die feste Eingabereihenfolge Hex, Decimal, Strings, Binary und Octal; der read-only Inspector ist kein Eingabehalt. Der kanonische EOF-Ort ist `(length / recordLength, length % recordLength)`. Bei einem partiellen letzten Record liegt die Append-Zelle deshalb direkt hinter dem letzten Byte; nur bei einer exakt recordlangen Datei entsteht die nächste Recordzeile. Backspace zeichnet den geänderten Pending-Wert unmittelbar. Enter übernimmt eine vollständige Pending-Eingabe und zeigt anschließend den neuen EOF-Ort; Enter ohne Pending-Eingabe am bestehenden EOF bleibt wegen der Längenbegrenzung wirkungslos und erzeugt weder Phantombyte noch Phantomzeile.

Der abschließende Clean-Binary-Selbsttest bestätigte alle fünf adressierbaren Datenpanes mit exakter Mausposition, den vollständigen Tab-/Shift-Tab-Zyklus, Maximize/Restore aller sechs Panes sowie Resize von 180×40 auf 160×35 und zurück. Eine 6-Byte-Datei wurde über Pending-Eingabe, Backspace und Enter zu exakt `ABCDEFA` erweitert; ein weiteres Enter am EOF änderte sie nicht. Mit dem 1-GB-Datensatz enwik9 waren nach 40 unmittelbar zugestellten Cursor-Down-Ereignissen, PageDown und acht Wheel-Schritten die Offsetzeilen aller fünf Datenpanes jeweils identisch. Der Nachlauf bis zum ruhigen Endzustand betrug im PTY-Lauf rund 0,13 s, 0,13 s und 0,17 s bei jeweils ungefähr 7,6 bis 8,2 KiB Terminalausgabe. Ein vermeintlicher Restzeichenfehler in Pane-Titeln erwies sich als chunkweise UTF-8-Dekodierung des temporären Prüfharnesses; ein inkrementeller Decoder und ein unabhängiger Mischburst zeigten vollständige Rahmen ohne Restzeichen. Dafür war keine Produktänderung erforderlich.

Die fünfte Sichtkorrektur trennt den Abschluss einer Pending-Editiertransaktion von der Navigation, die denselben Tastatur-Event ausgelöst hat. Im Insert-Modus fügt der Commit das neue Byte vor dem bisherigen Byte ein; dessen Verschiebung war daher kein zufällig mutiertes Zusatzbyte. Der Commit setzte den kanonischen Cursor bereits hinter den neuen Wert. Der bisherige Eventpfad verwendete diesen nachgezogenen Cursor anschließend erneut als Navigationsursprung und führte insbesondere bei Cursor-Rechts einen zweiten Schritt aus. Der neue Pfad sichert den ursprünglichen Edit-Offset vor dem Commit und berechnet Links, Rechts, Hoch, Runter, PageUp, PageDown, Home und End ausschließlich von diesem Ursprung. Ein Pane-Wechsel durch Tab oder Shift-Tab übernimmt den Wert, hält aber den kanonischen Byte-Offset. Enter bleibt bewusst der Commit-und-Weiter-Pfad. Die bestehenden Insert-/Overwrite-Regeln und ihre Einstellung über die VM werden nicht verändert.

Der fokussierte PTY-Selbsttest am finalen Clean-Binary begann jeweils mit `41 42 43 44 45 46` und Edit-Offset 2. Insert plus Cursor-Rechts ergab exakt `41 42 0a 43 44 45 46`, Overwrite plus Cursor-Rechts exakt `41 42 0a 44 45 46`; der Cursor stand in beiden Fällen auf Offset 3. Vollständige Hex-Eingabe `a5` lieferte dieselbe Geometrie mit dem Byte `a5`. Links und Home navigierten nach dem Insert genau von Offset 2 aus, Down erhielt die Spalte und klemmte am Dokumentende. Tab wechselte in beiden Modi in das Decimal-Pane, ließ den kanonischen Cursor aber auf Offset 2. Gespeicherte Bytes und sichtbare Werte waren in allen Läufen identisch.

Diese Korrektur erhöht das Workermaximum nicht. Eine gemeinsame Viewportänderung bleibt auf höchstens sechs gleichzeitig aktive Pane-Aufträge begrenzt; ein Pane hält weiterhin höchstens einen Auftrag und fasst Folgezustände zusammen. Der entfernte Cursor-Timer vermeidet pro offenem HexBento rund 2,22 Broadcasts je Sekunde. Stabile Cursor- und Fokusbewegungen erzeugen keine Datenpane-Worker und keinen vollständigen Canvas-Redraw; nur der cursorabhängige Inspector darf eine neue Projektion benötigen. Ein Klick während einer noch laufenden Projektionswelle kann deren inzwischen unpassendes Resultat verwerfen, erzeugt danach aber höchstens den bereits vorgesehenen zusammengefassten Folgeauftrag. Das größte Restrisiko der Sichtprüfung ist kein Worker-Churn, sondern ein kurzzeitiger Versatz bei unabhängig eintreffenden, versionsgleichen Pane-Projektionen. Eine atomare Sechs-Pane-Adoption würde die schnellen numerischen Panes an den langsamsten String-Payload koppeln und wird ohne sichtbaren Bedarf nicht eingeführt. Die TVision-Scrollbar stellt ihren Wert weiterhin als `int` dar; oberhalb `INT_MAX` logischer Records wird deshalb kein vollständig proportionaler Scrollbar-Support zugesichert. Der vorgeschriebene saubere Build `make clean all CXX=clang++` wurde vollständig und ohne Compilerwarnungen abgeschlossen. Vor der erneuten Sichtprüfung wurden keine Regressionchecks ausgeführt.

### 5.15 Implementierungsstand BENTO-001 bis BENTO-005

Problems, Structure und Functions besitzen jeweils einen endlichen Projektionstask mit dem konkreten Zielpane als `BentoPane`-Owner. Die Telemetrie unterscheidet Diagnosearbeit als `BDIAG` und Outline-Arbeit als `BOUT`. Pro fachlichem Pane ist höchstens ein Task aktiv; neuere Anforderungen werden als jüngster Folgezustand zusammengefasst. Parse-, Remap- und Format-Anforderungen der Diagnostik verwenden eine feste Priorität, sodass ein neuer Compileroutput nicht durch eine schwächere reine Formatierung verdrängt werden kann.

Beim Start eines normalen Build- oder Restart-Laufs hält die Diagnostik einen Snapshot der zu diesem Lauf gehörenden Source-Version fest. Eingehende Output-Chunks ändern zwar das Output-Dokument, lösen während des noch laufenden externen Auftrags aber keinen Parse-Worker aus. Erst das terminale Resultat gibt den externen Auftrag frei und fordert genau die Parse-Projektion für den vollständigen Output an. Ein währenddessen angefordertes F8 beziehungsweise Shift-F8 bleibt mit seiner Richtung vorgemerkt und wird erst nach einer aktuellen Adoption ausgeführt; ein nicht mehr herstellbarer Diagnosekontext wird stattdessen unmittelbar als nicht navigierbar quittiert.

Die Diagnoseprojektion parst Compileroutput, filtert Schweregrade, berechnet UTF-8-Spalten, führt Quelltextänderungen ausschließlich vorwärts über eine lückenlose, dokument- und versionsgebundene Folge aus altem Snapshot, neuem Snapshot und `DocumentChangeSet` fort und normalisiert Fehler- und Warnbereiche vollständig im Worker. Damit werden Positionen aus dem Build-Start-Snapshot auch über Source-Edits während des Compilerlaufs bis zur aktuellen Version remapped. Text, Diagnosen und Markerbereiche werden als unveränderliche `shared_ptr`-Payloads veröffentlicht. Stimmen Zielversion, Textlänge und Projektionstext-Hash bereits überein, entfällt die erneute Textadoption; Status und Marker werden weiterhin versionsgebunden aktualisiert. Alte `ReadSnapshot`-Instanzen bleiben durch die unveränderlichen Original- und Add-Buffer des Projektionsdokuments gültig.

Ein nicht über diese Änderungskette beobachteter Source-Commit besitzt keine verlässliche Offsetabbildung. In diesem Fall werden vorhandene Diagnosen und Marker verworfen und die Diagnosequelle als invalidiert markiert. Der alte Compileroutput wird nicht gegen den unbekannt veränderten Sourcezustand neu interpretiert; erst ein normaler Build oder Restart erzeugt wieder einen gemeinsamen Ausgangssnapshot. Dies ist die verbindliche Supportgrenze `BENTO-003`.

`ReadSnapshot` teilt Piece- und Original-/Add-Buffer, kann aber einen bereits materialisierten Text sowie Line-Index-Metadaten kopieren. Die vorwärts gerichtete Diagnosekette hält vorübergehend alte und neue Snapshots. Dieser temporäre Deep-Copy- und RAM-Churn ist als `BENTO-002` dokumentiert; er erzeugt keine persistente Registry und ist für diesen Korrekturzug kein funktionaler Abschlussblocker.

Für Structure und Functions erfasst die UI ausschließlich den bereits bestätigten Fold-Visible-State, den unveränderlichen Dokument-Snapshot und den Request als owning Input. Line-Texte, Fold-Spans und `ReadSnapshot` werden unter derselben Dokument-ID, Version und Fold-Revision erfasst und über den gemeinsamen Input-Cache von beiden Outline-Panes wiederverwendet. Bei höchstens 20.000 Zeilen und höchstens 8 MiB fordert der Bento-Pfad eine vollständige Fold-Akquisition an und ersetzt die partielle Projektion erst nach bestätigter vollständiger Abdeckung. Oberhalb einer der Grenzen bleibt die Outline absichtlich auf die bestätigten sichtbaren Fold-Spans begrenzt; `partial` ist dort der korrekte Zustand gemäß `BENTO-004`.

Sortierung, Duplikatunterdrückung, Hierarchieaufbau, Offsetauflösung und Textformatierung laufen im `BOUT`-Worker. Die Duplikatunterdrückung verwendet eine lokale O(log n)-Menge statt einer quadratischen linearen Suche. Ein Cancel-Flag wird bis in die Span-Schleife geführt; Rollenwechsel, Pane-Close und Bento-Close lassen einen obsoleten Worker deshalb kooperativ terminieren. Auch Outline-Adoptionen überspringen bei gleicher Zielversion, Textlänge und gleichem Projektionstext-Hash die erneute Textübernahme, ohne Status oder fachliche Einträge zu verlieren.

Jede Adoption prüft Task-ID, Owner-Art und ownerlokale Pane-ID, Generation, Rolle sowie Source-, Output- und Zieldokument einschließlich Version. Ein abweichendes oder verspätetes Resultat wird verworfen und kann keinen Taskzustand eines anderen Panes löschen. Fehler desselben unveränderten Schlüssels werden bis zu einer neuen externen Anforderung gelatcht; es entsteht kein Retry-Loop. Die zentrale Dispatch-Erweiterung enthält keine neue Registry und sucht das Ziel über die vorhandenen Fenster- und Pane-Beziehungen.

Die Adoptionssperre umfasst den vollständigen Diagnoseabschluss einschließlich Markerübernahme, Statusaktualisierung und Redraw. Zuvor war nur die Textübernahme geschützt. Eine dabei ausgelöste Minimap-Aktualisierung konnte deshalb über `cmUpdateTitle` reentrant eine neue `BDIAG`-Formatierung einreihen. Der äußere Abschluss setzte anschließend den Zustand auf aktuell, obwohl dieser neue Task noch aktiv war; F7, F8 und Shift-F8 warteten dadurch dauerhaft auf einen unerreichbaren Current-Zustand, während fortlaufend kurzlebige Worker entstanden. `BENTO-005` schließt diesen Feedbackpfad ohne Änderung der Keymap- oder Navigationssemantik.

Der isolierte Clean-Binary-Selbsttest erzeugte aus einem realen Compilerlauf ein befülltes Problems-Pane. Nach Rollenwechsel lieferte derselbe Bento-Sourcezustand 11 Structure- und 31 Functions-Einträge; die Projektionen erschienen ohne blockierenden UI-Nachlauf. Nach den abschließenden Lifecycle-, Remap-, Invalidierungs- und Vollakquisitionskorrekturen lief `make clean all CXX=clang++` erneut vollständig ohne Compilerwarnungen. Der daraus entstandene normale Binary bestand einen frischen Start, fünf Workspace-Restore-Wiederholungen und einen abschließenden Restore mit stabilem `Functions [31 items, full]`. Problems-Navigation, F7, F8, Shift-F8, Pane-Ownership, Restore, Interaktion und Worker-Abbau wurden in mehreren Sichtständen geprüft und vom Maintainer abgenommen. Der Read-only-Abschlussaudit fand keinen weiteren Funktionsblocker. Die Bento-Abnahme ist geschlossen und wird nicht aufgrund interner Unterbezeichnungen erneut geöffnet; Regressionchecks wurden nicht ausgeführt.

### 5.16 Implementierungsstand FCMP-001

Jede File-Compare-Generation besteht aus exakt fünf endlichen Compute-Aufträgen. Zwei pane-eigene Akquisitionsworker materialisieren und zerlegen die unveränderlichen Source-Snapshots unabhängig. Nach Adoption beider Akquisitionen berechnet genau ein geordneter Worker den Myers-Editpfad, normalisiert benachbarte Änderungen und bildet die Änderungsgruppen. Erst danach erzeugen zwei weitere pane-eigene Worker parallel die immutable Projektionen für Original und Compare. Der Myers-Frontier ist die technisch notwendige Ordnungsgrenze dieser Pipeline; er serialisiert ausschließlich den Diff derselben Generation und weder andere Bento-Owner noch die Compute-Lane.

Die UI übernimmt nur vollständige Payloads. Jede Stufe prüft Task-ID, Generation, Owner, Dokument-ID und Version. Eine Mutation startet eine neue Generation, cancelt alle noch bekannten Aufträge der alten Generation und verwirft verspätete Resultate. Read-only-Panes übernehmen vorbereiteten Text, Line-Kinds und Minimap-Slices über geteilte immutable Payloads. Editierbare Panes teilen weiterhin ausschließlich den kanonischen Dokumentzustand ihrer Source und übernehmen nur die abgeleiteten Marker. Synchrones Snapshot-Splitting, Myers-Berechnung und Projektionsformatierung sind aus dem UI-Thread entfernt. Snapshot-Erfassung sowie die begrenzte TVision-Adoption bleiben UI-affin.

Der Startpfad kopiert einen bereits erfassten `ReadSnapshot` nicht mehrfach. Die Submission hält genau eine owning Snapshot-Kopie je Akquisitionsworker. Innerhalb der vorhandenen `ReadSnapshot::text()`-Materialisierung besteht kein feinerer Cancel-Poll; ein bereits laufender Akquisitionsworker beendet deshalb noch dieses endliche Paket, bevor er das Cancel erkennt. Das blockiert die UI nicht und entspricht der verbindlichen Paketabschlussregel. Eine feinere Chunk-Materialisierung würde den zentralen Snapshotpfad verändern und wird ohne gemessene Abbruchlatenz nicht eingeführt.

Das Worker-Churn-Maximum beträgt fünf One-shot-Worker pro Generation, jedoch höchstens zwei gleichzeitig in der Akquisitionsphase, danach einen Diff-Worker und danach zwei Projektionsworker. Stabile Navigation erzeugt keine neue Generation. F8 und Shift-F8 bewegen nur die adoptierten Änderungsgruppen; Apply und tatsächliche Dokumentmutationen erzeugen erwartungsgemäß eine neue Fünfergeneration. Das verbleibende Churn-Risiko liegt damit in einer Folge sehr schneller Dokumentcommits, nicht in Scrollen, Zeichnen oder einem Retry-Loop.

Der isolierte PTY-Selbsttest bestätigte zwei Änderungsgruppen, sofort aktualisierten Pane-Status, F8-Navigation in beide Gruppen, beide Apply-Richtungen, Neuberechnung nach Mutation und Restore derselben File-Compare-Konfiguration aus einem autosaved Workspace. Ein identischer Vergleich zweier 954-MiB-Snapshots blieb während der Akquisition responsiv, wurde nach wenigen Sekunden vollständig projiziert und hinterließ nach dem Schließen weder Prozess- noch Worker-Reste. `make clean all CXX=clang++` wurde vollständig ohne Compilerwarnungen abgeschlossen; das daraus entstandene Binary bestand den Workspace-Restore erneut. Die Maintainer-Sichtprüfung nahm FCMP-001 ab und meldete ausschließlich einen kurz sichtbaren Initialframe mit normaler Editorsyntaxfarbe. Die Abschlusskorrektur aktiviert Palette und Gutter-Geometrie bereits bei Pane-Erzeugung; ein noch markerloser Compare-Canvas verwendet bis zur Ergebnisadoption `equal` als Darstellungsdefault. Der reproduzierte `[comparing]`-Zwischenframe zeigte damit bereits durchgehend das FC-Textattribut. Dafür entstehen weder ein Worker noch eine temporäre Line-Kind-Tabelle. Regressionchecks wurden nicht ausgeführt.

### 5.17 Abschlussentscheidung und Vergleichsmessung

Der freigegebene Funktionsscope dieses Korrekturzugs umfasst Worker-Substrat und Telemetrie, Line-Index, Display-Width, Syntax, Folding, Minimap, Bento- und HexBento-Projektionen, Goto, externe Quellen, Workspace-Lebenszyklen und File Compare. Die Bestandsmatrix enthielt zusätzlich vorsorglich alle gefundenen synchronen Pfade. Daraus entsteht jedoch kein stillschweigender Implementierungsauftrag. Insbesondere Multi-File-Search bleibt ein modaler Arbeitsablauf; eine asynchrone Suche würde den Dialog responsiver machen, aber keine parallele Editorbedienung ermöglichen. MFS und pauschales Datei-, Dialog- oder MRMAC-I/O werden erst aufgrund eines konkreten Nutzungsbefunds und eines neuen Auftrags zu einem eigenen Korrekturzug.

Die vorhandenen, während der einzelnen Abnahmen erhobenen Messungen ergeben folgenden Abschlussvergleich:

| Merkmal | Ausgangsbefund | Abschlussbefund | Bewertung |
|---|---|---|---|
| Core-Zuordnung | Affinitätsprobe mit fünf leeren Masken und `EINVAL`; Ordinal und tatsächlicher Core nicht korrelierbar | Mehr Worker als erlaubte Cores erfüllen einzeln `allowedCoreIds[workerOrdinal % allowedCoreIds.size()]`; kein Affinitätsfehler im Abschlussnachweis | Modulo-Vorgabe erfüllt |
| Lifecycle und Backlog | Erzeugung, Owner, Resultat und Terminierung nicht durchgängig korrelierbar | `created` bis `finished` sowie `accepted`, `adopted` und `discarded` sichtbar; Queue-, Result- und Live-Zähler kehren nach Last und Close auf null zurück | kein festgehaltener endlicher Worker und kein falscher Q-Sockel |
| Line-/WIDTH-Großdateipfad | enwik9-WIDTH-Lauf durch wiederholte Zeilenadressierung länger als sechs Minuten | 13.147.026 Zeilen in 16 lückenlosen Paketen zwischen 5,434 s und 6,384 s adoptiert; Debugger-Stopps inbegriffen | algorithmische Wiederholungsarbeit beseitigt |
| Syntax- und WIDTH-Churn | SYN bis 7,00 Pakete je Generation; 898 ownerlokale WIDTH-Resultate für 77 Logversionen | SYN 4,07 Pakete je Generation; WIDTH 65 Resultate für dieselben 77 Versionen, 92,8 Prozent weniger | Churn wesentlich reduziert; endliche Parallelität bleibt gewollt |
| Minimap | fokus- und Draw-getriebener persistenter Einzelworker, sichtbarer Nachlauf und Resultstau | 100 PageDown plus 100 PageUp im warmen Bereich ohne neue MAP-Welle; Fernsprung mit höchstens 16 Paketen und Rückkehr auf `MAP:0` | Ledger und Akquisitionsfenster begrenzen Doppelarbeit |
| Interaktion mit enwik9 | Restore, großer Sprung, Cursor-Autorepeat und Wheel konnten die UI für Sekunden bis Minuten blockieren | Restore und Chrome erscheinen unmittelbar; Cursor-Autorepeat, PageDown, Wheel und Großsprünge maintainer-seitig als performant und stabil abgenommen | primäres Nutzungsziel erreicht |
| HexBento | Formatierung und Stringanalyse im `draw()`-Pfad; paneübergreifende Redraws | fünf Datenpanes bleiben nach kombinierten Cursor-, PageDown- und Wheel-Bursts synchron; gemessener Nachlauf 0,13 s bis 0,17 s | teure Projektion aus dem Zeichenpfad entfernt |
| File Compare | Snapshot-Split und Projektion auf dem UI-Thread | identischer 954-MiB-Vergleich bleibt responsiv, ist nach wenigen Sekunden projiziert und schließt ohne Restworker | endliche 2→1→2-Pipeline abgenommen |
| Speicher | Kernbaseline 49.480 bis 50.304 KiB RSS; keine belastbare UI-Aufschlüsselung | kein dauerhaftes Wachstum oder Restbestand beobachtet; temporäre immutable Snapshots bleiben möglich | kein nachgewiesenes Leak; temporärer RAM-Peak ist nach Maintainer-Entscheidung kein primäres Schutzziel |

Eine hohe absolute Zahl kurzlebiger Worker ist für sich kein Fehler. Entscheidend sind begrenzte Pakete, nachvollziehbare Owner, strikte Modulo-Zuordnung, ausbleibende Lane-Serialisierung und der vollständige Rücklauf auf null. Genau diese Eigenschaften wurden im Panel, in isolierten PTY-Läufen und in der maintainer-seitigen Dauernutzung nachgewiesen. Die verbliebene parallele One-shot-Erzeugung wird deshalb nicht weiter vergröbert, solange weder Systemlast noch Interaktionslatenz einen neuen Befund liefern.

Bekannte technische Grenzen sind geschlossen entschieden: Eine außergewöhnlich lange Einzelzeile bleibt innerhalb ihres WIDTH-Pakets sequenziell; Syntax und Folding adoptieren zustandsbedingt geordnet; ein wirklich blockierender externer Stream darf bis EOF oder Close seinen eigenen Worker halten; HexBento beansprucht oberhalb `INT_MAX` logischer Records keinen vollständig proportionalen Scrollbar-Support; nach Header-Layoutänderungen bleibt bis zu einem gesonderten Buildmodell-Zug ausschließlich der vorgeschriebene Clean-Build belastbar. Diese Grenzen eröffnen keinen weiteren Unterzug dieses Korrekturzugs.

Prüfentscheidung: Der funktionale Korrekturzug ist maintainer-seitig abgenommen. Es besteht kein offener Implementierungs- oder Sichtprüfauftrag innerhalb seines freigegebenen Scopes. Der abschließende Clean-Build wurde vollständig und ohne Compilerwarnungen beendet. Damit sind alle Abschlusskriterien erfüllt.

## 6. Mess- und Instrumentierungsanforderungen

Jedes Worker-Ereignis trägt mindestens:

- monotone Zeit,
- den kanonischen `workerOrdinal` der Worker-Ausführung,
- OS-Thread-ID und zugewiesenen Core,
- Owner-Art und ownerlokale ID,
- Lane und Task-Art,
- Dokument-ID und Dokumentversion,
- Generation, Richtung und Paketspan, soweit anwendbar,
- Zustandswechsel `created`, `assigned`, `queued`, `running`, `result-ready`, `accepted`, `adopted`, `discarded`, `stopping`, `finished`,
- Queue-, Lauf-, Akzeptanz-, Adoptions- und Gesamtzeit,
- Abschlussgrund.

Das Performance Panel zeigt sowohl den aktuellen Zustand als auch einen begrenzten Ereignisverlauf. Diese begrenzte Lifecycle-Telemetrie bleibt Bestandteil des Betriebsmodells. Hochfrequente dateibasierte Detailtraces dürfen nur für ein benanntes Messziel aktiviert bleiben und werden nach dessen Nachweis wieder entfernt.

Pflichtszenarien:

1. Ein großes Dokument, ein Fenster, wiederholte große Cursor-Sprünge.
2. Drei Fenster desselben Dokuments mit gleichzeitigem Minimap-, Line-, Syntax- und Fold-Warm-up.
3. Mehr aktive Worker als erlaubte Cores zur Prüfung der Modulo-Folge.
4. Fokuswechsel zwischen Fenstern und Panes ohne Abbruch des Hintergrundfortschritts.
5. HexBento mit allen sechs Pane-Rollen und schnellen Cursor-/Scroll-Sprüngen.
6. Workspace Save/Restore mit mehreren Fenstern und Bento-Panes.
7. Live-Journal beziehungsweise externer Stream: normaler EOF, explizites Schließen, Abbruch und Fehler.

## 7. Umsetzungsprotokoll

| Datum | Gegenstand | Nachgewiesener Stand | Einschränkung / offener Beweis |
|---|---|---|---|
| 2026-07-19 | Prämissen und Gesamtzug | Dieses Prüfprotokoll angelegt; Syntax- und Folding-Ordnungsgrenzen sowie Bento-Owner aufgenommen | Noch keine Laufzeitänderung durch dieses Dokument |
| 2026-07-19 | Experimenteller Branch vor Unterzug 0 | Dynamische Worker-Registrierung, globaler `workerOrdinal`, Modulo-Affinität sowie erste separate Minimap- und Syntax-Worker sind im Arbeitsbaum erkennbar | Nicht abgenommen; Telemetrie unvollständig; Syntax weiterhin einzelner Owner-Worker; Bento unverändert; C++20-Workerstrukturen noch vorhanden |
| 2026-07-19 | HexBento-Bestandsbefund | String-Erkennung, Inspector-Aufbereitung und numerische Formatierung im UI-Zeichenpfad lokalisiert | Auslagerung und Latenzmessung stehen aus |
| 2026-07-19 | Unterzug 0, statische Inventur | Scan-, Warm-up-, Derived-, Bento-, Prozess-, Such-, Datei-I/O- und Restore-Pfade der Bestandsmatrix zugeordnet | Istzustand erfüllt BOF/EOF-, Ledger-, Pane-Owner- und Non-Blocking-Prämissen nicht |
| 2026-07-19 | Unterzug 0, Prozessbaseline | Kernprüfung gebaut und gemessen: 24/25 bestanden; 1,72 bis 4,05 s Wall Time; 49.480 bis 50.304 KiB RSS | File-Compare-Navigation bereits vor dem Korrekturzug rot; keine UI-Latenzauflösung |
| 2026-07-19 | Unterzug 0, Affinitätsbaseline | Auf vier erlaubten CPUs 957 erfolgreiche Bindungen nahezu gleichmäßig verteilt; maximal 30 Threads gleichzeitig beobachtet | fünf leere Masken mit `EINVAL`; keine Ordinal-/Owner-Korrelation; Modulo-Prüfziel nicht bestanden |
| 2026-07-19 | Unterzug 0, Hardware-Counter | `perf` 7.1.3-1 und `libpfm` installiert; drei User-Space-Messläufe mit stabilen Cycles-, Instructions-, Branch- und Cache-Zählern protokolliert | Kernel-Counter bleiben bei `perf_event_paranoid=2` ausgeschlossen; keine Kernel-Einstellung geändert |
| 2026-07-19 | Unterzug 1, Lifecycle-Telemetrie | Strukturierter Ring und Snapshot für Worker, Ordinal, OS-Thread, Core/Fehler, Owner, Task, Zeit und alle neun Lifecycle-Zustände implementiert | Ring ist bewusst auf 4.096 Ereignisse begrenzt; Instrumentierungsaufwand bleibt bis zum Abschlusszug aktiv |
| 2026-07-19 | Unterzug 1, Result-Adoption | Tatsächliche Apply-/Versionsentscheidung für Line, Syntax, Fold, Minimap, Save-Normalisierung, File Compare, externe Chunks und staged Macros erfasst | Unbekannte beziehungsweise unbehandelte Completed-Payloads werden sichtbar verworfen |
| 2026-07-19 | Unterzug 1, Performance Panel | Vier positionsstabile Ereignisslots mit relativer Ereigniszeit, Update im vorhandenen Slot, Verdrängung des ältesten Slots bei einem bislang unsichtbaren Worker, sichtbarem `finished`, Owner-Zählung, sekundenbezogenen Lifecycle-Aggregaten und Braille-Subzellenmeter implementiert und maintainer-seitig abgenommen | Die Detaildarstellung ist bewusst auf die vier zuletzt berührten Worker-Identitäten begrenzt; Zeitablauf ohne Ereignis verändert keinen Slot |
| 2026-07-19 | Unterzug 1, Zwei-Core-Probe | sieben Worker auf zwei CPUs; sämtliche sieben Lifecycle-Ketten vollständig; strikte Ordinal-Modulo-Prüfung und null Affinitätsfehler | Prüft die Instrumentierung, noch nicht das C++18-Substrat oder alle Owner-Pipelines |
| 2026-07-19 | Unterzug 1, Build und Messung | sauberer und inkrementeller Clang-Build ohne Warnungen; Kernprüfung weiterhin 24/25; drei `perf`-Läufe ohne erkennbare Verschlechterung | vorhandener File-Compare-Fehler unverändert; UI-Latenzmessung folgt später |
| 2026-07-19 | Display-Width-Zwischenzug | synchronen UI-Vollscan entfernt; pro Editorgeneration bis zu `allowedCoreCount` endliche WIDTH-Paketworker, O(Core)-Ledger und versionierte Out-of-order-Adoption implementiert; Paketmetadaten-Regressionsprüfung sowie sauberer Clang-Abschlussbuild ohne Warnungen bestanden | quantifizierte Wheel-/Held-Cursor-Latenzmessung bleibt offen; Einzelzeilenberechnung ist sequenziell |
| 2026-07-19 | Display-Width-Debuggerbefund | 16 WIDTH-Worker auf 16 Cores und tatsächlicher Fortschritt ohne Deadlock nachgewiesen; Line-Lookup, Zeilentext und Display-Width semantisch getrennt; Verbraucher der Zeilendaten inventarisiert | wiederholtes `lineStartByIndex()` verursacht bei Checkpoint-Stride 4.096 ungefähr 26,9 Milliarden `nextLine()`-Aufrufe; lineare Korrektur und erneute Messung stehen aus |
| 2026-07-19 | Display-Width-Linearisierung | Paketstart einmal per `lineStartByIndex()` adressiert und anschließend nur mit `nextLine()` fortgeschritten; finale enwik9-Generation mit 13.147.026 Zeilen in 16 lückenlosen Paketen außer Reihenfolge vollständig zwischen 5,434 und 6,384 Sekunden adoptiert | Debugger-Stopps sind in der Zeit enthalten; Wheel-/Held-Cursor-Latenz bleibt separat offen |
| 2026-07-19 | Identitätsmodell | Redundanz und missverständliche Nähe von Worker-ID, Worker-Ordinal, Owner-ID, Task-ID, OS-Thread-ID und Created-Summe als eigener Konsolidierungsauftrag in Unterzug 2 aufgenommen | kanonischer Worker-Bezeichner und endgültige Panel-Nomenklatur werden vor der Implementierung des Unterzugs festgelegt und geprüft |
| 2026-07-19 | Einschub Workspace/CLI-Fokus | Bei aktivem Workspace-Autorestore wird der Workspace vor den Kommandozeilendateien restauriert. Ein kanonisch pfadgleiches restauriertes Fenster wird fokussiert, ohne ein zusätzliches Fenster zu erzeugen; nicht vorhandene Dateien werden weiterhin geöffnet. Ein isolierter Start mit drei gespeicherten `enwik9`-Ansichten und zusätzlichem CLI-`enwik9` blieb nach Autosave bei exakt drei Einträgen. | Absichtlich mehrfach gespeicherte Ansichten und manuelles additives Workspace-Laden bleiben unverändert; der vorhandene File-Compare-Navigationsfehler im Core-Satz ist sachfremd und weiterhin offen. |
| 2026-07-19 | Worker-Endzustand | Reguläres Ende eines endlichen Workers in Enum, Lifecycle-Grund, Telemetriezähler und Performance Panel einheitlich von `terminated` auf `finished` umbenannt; der Coprocessor-Harness weist ausgeglichene `created`-/`finished`-Summen sowie `WorkerFinished` und `StopRequested` dynamisch nach | `terminated by signal` bleibt ausschließlich die Diagnose einer tatsächlichen Prozessbeendigung durch Signal |
| 2026-07-19 | Unterzug 2, C++18-Substrat | Worker-Erzeugung, Loop, Stop, Join und Retirement in `MRCoprocessorWorkerLifecycle.cpp` abgegrenzt; `std::thread`, direkter Stop-Zustand und `condition_variable` ersetzen `jthread`/`stop_token` | fachliche Paket- und Ledger-Pipelines bleiben Unterzug 3 bis 8 |
| 2026-07-19 | Unterzug 2, Identitäten und Submission | `workerOrdinal` ist einziger Worker-Bezeichner; Owner-Paar, Task-ID, OS-Thread-ID und Core sind getrennt; wirkungsloses One-shot-Coalescing entfernt | Workspace-Rekonstruktion aller Owner bleibt Unterzug 9 |
| 2026-07-19 | Unterzug 2, dynamischer Nachweis | `allowedCoreCount + 3` gleichzeitige Same-Lane-Worker erreichen gemeinsam die Schranke und erfüllen einzeln die exakte Ordinal-Modulo-Formel; persistenter Worker liefert nach Deregistrierung Cancelled, `Finished(StopRequested)` und wird gejoint; fünf Wiederholungen, MRMAC-v1-Suite und sauberer Clang-Build ohne Warnungen bestanden | Core-Suite bleibt ausschließlich wegen des bekannten File-Compare-Navigationsfehlers bei 26/27 |
| 2026-07-19 | Unterzug 3, SIMD-Line-Scan | Acht-MiB-Bytepakete, mehrere BOF-/EOF-Worker je Editorgeneration, dokumentgebundene Reservierungen und Out-of-order-Ledger implementiert; IDX-Ereignisse tragen Generation, Richtung und Bytespan | `make clean all CXX=clang++` ohne Warnungen; Sichtprüfung und Messung ausstehend; keine Regressionchecks vor Abnahme |
| 2026-07-19 | Unterzug 3, Restore-UI-Fallback | Debugger und Laufzeitprotokoll belegen wiederholte synchrone Fernscans aus Syntax-Scheduler und Viewport; Syntax wartet auf den exakten Ledger, der Viewport zeichnet bis dahin vom lokalen Cursoranker und ohne zeilenweise `lineIndex()`-Neuberechnung | inkrementeller Clang-Sichtprüfungsbuild ohne Warnungen; erneuter kalter enwik9-Restore steht maintainer-seitig aus; keine Regressionchecks ausgeführt |
| 2026-07-19 | Unterzug 3, Interaktions-Latenz | Zehn Sekunden Cursor-Autorepeat: 42 Fold-, 23 Minimap-, 12 Syntax-Neuplanungen und 61 Syntax-Zielerweiterungen; lokale Vertikal-/Wheel-Navigation und Pending-Fold-Abdeckung korrigiert | neuer inkrementeller Clang-Sichtprüfungsbuild ohne Warnungen; Cursor-/Wheel-Nachprüfung steht aus; Minimap und Syntax bleiben eigene Unterzüge |
| 2026-07-19 | Unterzug 3, Sichtabnahme | Maintainer bestätigt instantanen Restore und Chrome-Aufbau sowie korrigiertes Cursor-Autorepeat und Wheel-Verhalten in enwik9 | Sichtprüfung bestanden; technische Abschlussprüfung und Messprotokoll folgen nach der Abnahme |
| 2026-07-19 | Einschub XML-Folding | Same-Line-Leaf-Tags werden nicht mehr als offene Fold-Strukturen registriert; namenspassende Abschlüsse hinter Nutztext schließen den offenen Stack; offene XML-Blöcke werden nicht an Scanfenstergrenzen abgeschlossen; geschlossene Spans werden bei Adoption revalidiert; Fold-Zeichnung und Navigation iterieren lokal | sauberer Clang-Sichtprüfungsbuild ohne Warnungen; enwik9-Prüfziel 3.114.233 → 3.114.395 und Wheel-Prüfung mit geschlossenen Folds maintainer-seitig bestanden; technische Nachprüfung folgt erst nach dieser Abnahme; allgemeine Fold-Checkpointkette bleibt `FOLD-001`/`FOLD-002` |
| 2026-07-19 | Unterzug 3, technische Abschlussprüfung | Coprocessor-/Modulo-, Deferred-Line-Index-, Piece-Table-, Block-Markierungs- und EOF-Prüfungen bestanden; kleine Dokumente umgehen die Großdateischätzung; Post-EOF-Guard an lokale Fold-Iteration angepasst | Core-Suite 26/27; ausschließlich der vor dem Korrekturzug bekannte File-Compare-Navigationsfehler bleibt rot; allgemeine Syntax-, Folding- und Minimap-Pipelines folgen in Unterzug 4 bis 6 |
| 2026-07-19 | Unterzug 4, Syntax-Paketpipeline | Persistenten Syntax-Worker entfernt; endliche generationsgebundene BOF-/EOF-Pakete, sparse Checkpoints, vorläufiger Result-Ledger, bestätigte Vorwärtsadoption und exaktes Owner-Routing implementiert | `make clean all CXX=clang++` ohne Warnungen; enwik9-, Mehrfenster- und Responsivitäts-Sichtprüfung ausstehend; keine Regressionchecks vor Abnahme |
| 2026-07-19 | Unterzug 4, SYN-Lognachweis | Jeder beendete Syntaxauftrag protokolliert Task, Worker, Owner, Core und Affinitätsstatus, OS-Thread, Generation, Richtung, Paketspan, Ergebnis- und Dispatchstatus, Queue-/Lauf-/Gesamtzeit sowie Token- und Checkpointzahl | `make clean all CXX=clang++` ohne Warnungen; Log-Sichtprüfung ausstehend; keine Regressionchecks vor Abnahme |
| 2026-07-19 | Unterzug 4, Syntax-Adoptionssemantik | `result-ready`, `accepted` und `adopted` getrennt; kompakte Taskidentität bis zur Checkpointentscheidung erhalten; spätere Zustandsablehnung als `discarded` vor dem Retry sichtbar | `make clean all CXX=clang++` ohne Warnungen; Sichtprüfung ausstehend; keine Regressionchecks vor Abnahme |
| 2026-07-20 | Unterzug 4, Syntax-Worker-Churn | Bereits bestätigten Viewportzustand als direkten Kontextanker verwendet; redundante BOF-Einzeilenpakete entfernt; notwendige Abdeckung vom doppelten Prefetch-Ziel getrennt; `required` und `target` im Laufzeitlog ergänzt | `make clean all CXX=clang++` ohne Warnungen; enwik9-, Großsprung- und Mehrfenster-Sichtprüfung ausstehend; keine Regressionchecks vor Abnahme |
| 2026-07-20 | Unterzug 4, exakte Owner-Zuordnung | Endliche Resultate tragen ihre Worker-Lebensdauer explizit und protokollieren Task, Worker, konkreten `OwnerKind:localId`, Core, Dokument/Version, Generation, Richtung, Span und Laufzeiten; hochfrequente Warmup-, Syntax-, Panel- und VM-Hotspot-Diagnosen schreiben file-only und verändern nicht mehr das MR-LOG-Editor-Dokument | `make clean all CXX=clang++` ohne Warnungen; Sichtprüfung und Auswertung der neuen `Coprocessor finite result`-Zeilen in `/tmp/mr.log` ausstehend; keine Regressionchecks vor Abnahme |
| 2026-07-20 | Unterzug 4, gemessene Churn-Optimierung | Eine verifizierte Kette reiner EOF-Appends erhält auch noch laufende WIDTH-Präfixpakete mit ihrer jeweiligen Quellversion und Generation. Der Tail bleibt bis zum exakten Line-Index vorgemerkt und wird dann als ein endliches Paket materialisiert; mehrere bis dahin eingegangene Appends werden vom frühesten betroffenen Zeilenanfang bis zum aktuellen EOF zusammengefasst. Andere Änderungen verwerfen die Kette und fallen auf den vollständigen versionierten Scan zurück. Das Syntax-Paketziel wurde von 16 auf 32 Zeilen erhöht, ohne Checkpoint- oder Adoptionssemantik zu verändern. | `make clean all CXX=clang++` ohne Warnungen; erneute enwik9-/MR-LOG-Sichtmessung der nachgeschärften Append-Kette ausstehend; keine Regressionchecks vor Abnahme |
| 2026-07-20 | Unterzug 4, erste Churn-Sichtmessung | Maintainer meldet unveränderte Systemlast, sehr performantes Compilat und niedrige Säulen. Im Intervall 11:32:34 bis 11:33:38 sank SYN trotz 309 statt 292 Generationen von 2.045 auf 1.241 Resultate, entsprechend von 7,00 auf 4,02 Pakete je Generation. | Die erste WIDTH-Fassung verlangte einen bereits vollständigen Vorgängerzustand; bei schnellen MR-LOG-Appends stieg deren Resultzahl deshalb von 914 auf 962. Dieser Messbefund führte zur nachgeschärften In-flight-Präfixübernahme im aktuellen Sicht-Build. |
| 2026-07-20 | Unterzug 4, zweite Churn-Sichtmessung | Maintainer meldet erneut unauffällige Systemlast und keine Funktionsprobleme. SYN bleibt mit 659 Resultaten für 145 Generationen beziehungsweise 4,54 Paketen je Generation im Zielbereich. | WIDTH erzeugte für 77 MR-LOG-Versionen weiterhin 898 ownerlokale Resultate. Ursache: Der korrekte EOF-Insert invalidiert zunächst den exakten Line-Index; die WIDTH-Übernahme fiel vor dessen Adoption auf Vollscan zurück. Der neue Sicht-Build hält deshalb den Tail vorgemerkt und materialisiert ihn erst nach der Line-Index-Adoption. |
| 2026-07-20 | Unterzug 4, dritte Churn-Sichtmessung | Für 77 MR-LOG-Versionen entstanden nach der verzögerten Tail-Materialisierung nur noch 65 ownerlokale WIDTH-Resultate statt 898, eine Reduktion um 92,8 Prozent. Einschließlich enwik9 wurden 81 WIDTH-Resultate vollständig abgeschlossen. SYN blieb mit 362 Resultaten für 89 Generationen beziehungsweise 4,07 Paketen je Generation im Zielbereich. | Maintainer meldet keine Funktionsprobleme, unauffällige Systemlast und ein sehr performantes Compilat. Die hochfrequente Dateiinstrumentierung hat ihr Messziel erfüllt. |
| 2026-07-20 | Unterzug 4, Instrumentierungsrückbau | Paketweise Result-, Syntax- und Large-file-Warmup-Traces, periodische One-shot- und VM-Hash-Gesamtstände sowie die verbliebene `Phase1 doc`-Zeitmessung in sämtlichen TextDocument-Hotpaths entfernt. Das abgenommene Performance Panel, der begrenzte Lifecycle-Ring und die explizit aktivierbare `MR_TRACE_WARMUP_CANCEL`-Diagnose bleiben erhalten. | Der Loglauf um 12:20 Uhr enthielt keine neuen paketweisen Traces oder Fehler, legte aber 107 Aufrufe der anschließend entfernten `Phase1 doc`-Sonde offen. `make clean all CXX=clang++` danach ohne Warnungen; keine Regressionchecks vor Abnahme. |
| 2026-07-20 | Unterzug 5, Fold-Paketpipeline | Endliche generationsgebundene FOLD-Pakete, vollständiger Strukturzustand, eine Look-ahead-Zeile, vorläufiger Result-Ledger, bestätigte Vorwärtsadoption, paketgrenzensichere Veröffentlichung und exaktes Owner-Routing implementiert | `make clean all CXX=clang++` ohne Warnungen; Sichtprüfung und kanonischer Ankernachweis ausstehend; der begrenzte Fernsprung-Kontext ist ausdrücklich noch kein allgemeiner BOF-Beweis; keine Regressionchecks vor Abnahme. |
| 2026-07-20 | Unterzug 5, Lifecycle-Sichtprüfung | Maintainer bestätigt sichtbare Entstehung von FOLD-Workern sowie Adoption und Rejection vorläufiger Fold-Resultate | Paket- und Adoptionsmechanik sichtbar bestanden; Fold-Spans, Interaktionsverhalten und kanonischer Fernsprunganker bleiben getrennte Prüfziele; keine Regressionchecks ausgeführt. |
| 2026-07-20 | Unterzug 5, dokumentweite Fold-Welle | Rechtsklick erzeugt eine versionierte BOF→EOF-Welle aus höchstens `allowedCoreCount` gleichzeitig materialisierten endlichen FOLD-Paketen; geordnete Strukturadoption erzeugt anschließend in einem FOLD-Worker eine kompakte Dokumentprojektion mit kleinen lokalen Öffnungsausnahmen | `make clean all CXX=clang++` ohne Warnungen; maintainer-seitige Prüfung von UI-Latenz, Worker-Lifecycle sowie BOF-/Mitte-/EOF-Projektion ausstehend; keine Regressionchecks vor Abnahme. |
| 2026-07-20 | Unterzug 5, Ersatz der verworfenen Fold-Welle | Spekulative Strukturpakete wurden durch parallele, zustandslose Zeilentextakquisition und genau einmal checkpointgeordnet ausgeführte Strukturvalidierung ersetzt. Die am Viewport gewählte Ebene wird über einen bestätigten Anker auf die kanonische BOF-Tiefe abgebildet. Die flache Grundprojektion schließt die gewählte Ebene einschließlich aller tieferen Nachfahren; beim expliziten Öffnen eines Elternspans liefert ein kleines Viewport-Overlay die weiterhin geschlossenen direkten Nachfahren. Kanonische Großprojektionen werden bei Adoption nicht im UI kopiert oder sortiert. Der für XML wirkungslose quadratische Smart-Dedent-Suffixpfad entfällt. Zusammenhängendes Viewport-Fold-Warm-up bleibt bei aktiver Dokumentprojektion lokal begrenzt und läuft nicht mehr über Millionen bereits ausgeblendeter Zeilen. | `make clean all CXX=clang++` vollständig und ohne Warnungen; Sichtprüfung von Ebenensemantik, Akquisitions-/Validierungsereignissen, Resultzahl, Wheel-Verhalten sowie BOF/Mitte/EOF steht aus; keine Regressionchecks vor Abnahme. |
| 2026-07-20 | Unterzug 5, progressive Fernprojektion | Die einmalige BOF→Viewport-Kontextauswertung liefert wiederverwendbare Struktur-Checkpoints. Ab dem bestätigten Anker läuft die Vorwärtskette weiter, während Präfix-Projektionspakete aus diesen Checkpoints parallel arbeiten. Die dokumentweite Projektion wird als Folge unveränderlicher Segmente fortschreitend adoptiert; ein Fernsprung aktualisiert das Ziel und erhält sofort eine lokal validierte Vorschau. | Automatisierter PTY-Nachweis: enwik9-Nahbereich in 3,945 s foldbar, Fernbereich nach Sprung in 3,818 s geschlossen, kanonische Welle nach 70,7 s beendet, geöffneter Elternspan behält geschlossene Nachfahren. Ein separates abgeschnittenes XML faltet die vollständige `revision`, lässt die unvollständige EOF-`revision` aber offen. `make clean all CXX=clang++` ohne Warnungen; keine Regressionchecks ausgeführt. |
| 2026-07-20 | Unterzug 6, Minimap-Paketpipeline | Persistenten Minimap-Worker entfernt; endliche owner-, generations-, richtungs- und spangebundene Text- und Overlay-Pakete, ein auf `O(Core)` begrenzter Warmfenster-Ledger, exaktes Task-Owner-Routing und immutable Adoption implementiert; Range-Signaturen und Overlay-Projektion aus `draw()` entfernt | PTY-Selbsttest mit vier restaurierten Editoren, gleichzeitigen Hintergrund-Ownern, responsivem Cursor-Down/PageDown, sichtbarer Großsprung-Welle und vollständiger Rückkehr auf `live:0`/`MAP:0` bestanden; `make clean all CXX=clang++` ohne Warnungen; die folgende Sichtprüfung verwarf den Result-Churn; keine Regressionchecks ausgeführt. |
| 2026-07-21 | Unterzug 6, Minimap-Churn-Korrektur | Richtungsgewichtete Akquisitionsfenster über `max(8, 4 * allowedCoreCount)` Viewports, Übernahme fertiger Zeilen aus laufenden Generationen, spanexakte Reservierungswiederverwendung und sofortige Adoption des fertigen sichtbaren Teilfensters implementiert | 80×24-PTY: 100 PageDown plus 100 PageUp ohne neue MAP-Welle im bereits akquirierten Bereich; Fernsprung mit höchstens 16 MAP-Paketen und Rückkehr auf `MAP:0`. Verbleibender Nachlauf ist SYN/FOLD/WIDTH. Temporärer RAM-Peak ist kein primäres Schutzziel; globale Eventpfad-Adoption war nicht erforderlich. `make clean all CXX=clang++` vollständig ohne Compilerwarnungen; maintainer-seitige Latenzprüfung ausstehend; keine Regressionchecks ausgeführt. |
| 2026-07-21 | Unterzug 6, Sichtabnahme | Maintainer bestätigt sehr performantes Verhalten bei der enwik9-Interaktion sowie die erwartungsgemäße Nutzung und zeitnahe Entleerung der Queue | Sichtprüfung bestanden; technische Abschlussprüfung darf nun getrennt folgen. |
| 2026-07-21 | Bento 1.5, Pane-Widget-Vorbereitung | Geschlossene Widget-Maske für Fold-Gutter und Minimap je Bento-Leaf; `bento=v1.5`-Writer und rückwärtskompatibler `v1`-Import über den bestehenden Workspace-Pfad | Sichtprüfung von Legacy-Restore und neuem Bento-/HexBento-Roundtrip ausstehend; keine Worker- oder Folding-Semantik geändert; keine Regressionchecks ausgeführt. |
| 2026-07-21 | Unterzug 7B, Hex-Pane-Projektion | Sechs eigenständige `HX`-Owner mit endlichen immutable Projektionsworkern; rollenabhängige Rechenäquivalenz, exakte Result-Route, Commit-Broadcast und reale Hintergrundgeometrie maximierter Restore-Panes implementiert | `make clean all CXX=clang++` vollständig und ohne Compilerwarnungen; maintainer-seitige Prüfung von Ownern, Churn, Adoption, Undo/Redo/Revert, Endian, EOF-Edit sowie Maximize/Restore ausstehend; keine Regressionchecks ausgeführt. |
| 2026-07-21 | Unterzug 7B, erste Sichtkorrektur | Gemeinsamer Record-/Spaltenanker und projizierter Blockcursor für fünf Datenpanes; versionsgebundene Altprojektion statt leerem Canvas; commitfester Hex-Cursor; separate EOF-Append-Zeile; Pane-Chrome vor Divider; Timer-Cleanup und einmaliger Canvas-Draw | `make clean all CXX=clang++` vollständig ohne Compilerwarnungen; erneute Sichtprüfung von Positionssync, Flackerfreiheit, Maximize, Undo, EOF-Append und Worker-Churn ausstehend; atomare Sechs-Pane-Adoption nicht eingeführt; Scrollbar-Skalierung oberhalb `INT_MAX` offen; keine Regressionchecks ausgeführt. |
| 2026-07-21 | Unterzug 7B, zweite Sichtkorrektur | Verworfenen paneübergreifenden Blinktimer entfernt; statische Byte-Markierung in allen sichtbaren Darstellungen und ausschließlich nativer TVision-Cursor im fokussierten Pane; Hit-Test an den tatsächlich dargestellten Payload gebunden; Mouse-Aktivierung bleibt an dessen sichtbarem Anker; Cursor-Reveal verwendet nur die aktive Pane-Kapazität | `make clean all CXX=clang++` vollständig ohne Compilerwarnungen; erneute Sichtprüfung von Fokus, exakter Mausadressierung während Pending-Adoption, Tab/Ctrl-Tab, EOF-Append sowie Worker-Churn ausstehend; keine Regressionchecks vor Abnahme. |
| 2026-07-21 | Unterzug 7B, dritte Sichtkorrektur | Fehlenden Child-Draw im gepufferten `MRHexPaneWindow` wiederhergestellt; kanonischer Cursor, Edit und Append werden damit vom vorhandenen Bento-Content-Flush unmittelbar sichtbar | isolierter 180×40-PTY-Selbsttest für alle fünf Datenpanes, Pfeiltaste und 6→7-Byte-EOF-Append vor und nach dem Clean-Build bestanden; `make clean all CXX=clang++` vollständig ohne Compilerwarnungen; erneute Maintainer-Sichtprüfung ausstehend; keine Regressionchecks vor Abnahme. |
| 2026-07-22 | Unterzug 7B, vierte Sichtkorrektur | Stabile Cursor-, Fokus- und Resultänderungen auf lokale Recordzeilen begrenzt; generischen Bento-Content-Flush für lokal projizierende Hex-Panes unterdrückt; feste Tab-/Shift-Tab-Reihenfolge; kanonischen inline-EOF-Ort und unmittelbare Pending-Edit-Darstellung umgesetzt | unabhängige Clean-Binary-PTY-Läufe: exakte Klickadressierung in fünf Datenpanes, vollständige Pane-Zyklen, Maximize/Restore aller sechs Panes, Resize, Backspace/Enter/EOF sowie synchroner enwik9-Nachlauf bestanden; `make clean all CXX=clang++` vollständig ohne Compilerwarnungen; Maintainer-Sichtprüfung ausstehend; keine Regressionchecks vor Abnahme. |
| 2026-07-22 | Unterzug 7B, fünfte Sichtkorrektur | Pending-Navigation verwendet den vor dem Commit gesicherten Edit-Offset; Cursor-Rechts vollzieht damit exakt einen Schritt; die übrigen Navigationstasten erhalten Zeile und Spalte relativ zum Editierursprung; Tab/Shift-Tab committen ohne Byteversatz | Fokussierter Clean-Binary-PTY-Selbsttest für Insert, Overwrite, ein- und zweistellige Hex-Eingabe, Rechts, Links, Home, Down und Tab mit exakten Bytefolgen und Cursorpositionen bestanden; `make clean all CXX=clang++` vollständig ohne Compilerwarnungen; Maintainer-Sichtprüfung ausstehend; keine Regressionchecks vor Abnahme. |
| 2026-07-22 | Unterzug 5, kanonischer Fernsprunganker (`FOLD-002`) | Dokument-, versions- und sprachgebundener BOF-Checkpointledger; parallele 65.536-Zeilen-Akquisition; genau einmal geordnete Strukturvalidierung; markerfreier Fernviewport bis zum bestätigten Kontextanker; requestgebundene Fehler- und Supersessionsbehandlung | Isolierter enwik9-PTY-Selbsttest bis zum Bereich um Zeile 13.000.000: korrekte Paketspans, anschließend validierte XML-Foldmarker und Rückkehr der FOLD-Aktivität auf null; kanonische Kette rund 49 s im Hintergrund; `make clean all CXX=clang++` vollständig ohne Compilerwarnungen; Maintainer-Sichtprüfung bestanden; keine Regressionchecks ausgeführt. |
| 2026-07-22 | Unterzug 1, Q-Telemetriereihenfolge | Das `Queued`-Lifecycle-Ereignis wird nach erfolgreicher Queue-Publikation, aber noch unter demselben Lane-Mutex erfasst. Ein One-shot-Worker kann deshalb nicht mehr `Running` vor `Queued` protokollieren und einen falschen permanenten Q-Basiswert hinterlassen. Queue-, Worker- und Panel-Semantik bleiben unverändert. | Live-Debugger vor der Korrektur: keine Worker, Retiring-Worker oder Resultate, aber `telemetryQueuedTaskCount=1`. Danach im Clean-Binary-PTY 120 schnelle PageDown-Ereignisse in enwik9 und vollständige Rückkehr von Queue-, Aktiv-, Result-, Worker- und Resultzählern auf null; `make clean all CXX=clang++` vollständig ohne Compilerwarnungen; Maintainer-Sichtprüfung bestanden; keine Regressionchecks ausgeführt. |
| 2026-07-22 | Unterzug 8A, deferred Goto Line (`NAV-001`) | Die synchrone BOF-Schleife in `handleSearchGotoLineNumber()` ist entfernt. Ein noch nicht durch den bestätigten Line-Index auflösbares Ziel bleibt als dokument-, versions-, cursor- und selektionsgebundener Zustand beim Editor vorgemerkt. Der vorhandene IDX-Warm-up arbeitet unverändert weiter; nach dessen Adoption wird das Ziel exakt angesprungen. Eine neuere Navigationsanforderung ersetzt ausschließlich dieses noch nicht ausgeführte Ziel, nicht queued oder laufende IDX-Worker. | Isolierter kalter enwik9-PTY-Selbsttest sprang während sichtbarem Index-Warm-up responsiv und exakt auf Zeile 13.000.000. Maintainer bestätigt performantes und verlässliches Compilat; die kurze IDX/Q-Aktivität war zwischen gleichzeitigen FOLD- und weiteren Aufträgen nicht isoliert sichtbar und der technische Selbsttest wurde als Nachweis freigegeben. `make clean all CXX=clang++` vollständig ohne Compilerwarnungen; keine Regressionchecks ausgeführt. |
| 2026-07-22 | Unterzug 8B, Line-Drawing-Linearisierung (`LINE-002`) | `materializeLineDrawingRows()` adressiert den Anfang eines zusammenhängenden Zeilenbereichs genau einmal über `lineStartByIndex()` und schreitet danach über `nextLine()` fort. Transaktions-, Cursor-, Tabulator- und EOF-Materialisierungssemantik bleiben unverändert. | Reale Editor-Selbsttests materialisierten eine Column-Box über eine Leerzeile sowie fünf tabulatorhaltige Zeilen korrekt. `make clean all CXX=clang++` vollständig ohne Compilerwarnungen; Maintainer-Sichtprüfung bestanden; keine Regressionchecks ausgeführt. |
| 2026-07-22 | Unterzug 8C, tote Task-Arten (`DEAD-001`) | Die niemals submitierten Task-Arten `SaveNormalizationWarmup` und `IndicatorBlink` samt Payloads, Dispatch-, Telemetrie-, Fenster- und Editor-Restzuständen wurden entfernt. Die synchrone Save-Normalisierung einschließlich Cache und Durchsatzmessung sowie die vorhandenen UI-Timer der Indikatoren bleiben unverändert. | Isolierter Editor-Selbsttest speicherte nach Mutation exakt `Xalpha\n`; Insert-Indikator und UI blieben responsiv. `make clean all CXX=clang++` vollständig ohne Compilerwarnungen; Maintainer-Sichtprüfung bestanden; keine Regressionchecks ausgeführt. |
| 2026-07-22 | Unterzug 8D, externe Quellen (`EXT-001`) | Registrierte Journal- und Live-Log-Quellen behalten ihren exklusiven EX-Worker nur für die Lebensdauer der blockierenden Quelle. Journal-Pipes arbeiten nichtblockierend; Cancel schließt die lokale Leseseite, terminiert die gesamte Prozessgruppe mit begrenzter TERM/KILL-Eskalation vor dem Reap und verhindert Dauerinput-, HUP- und Nachkommen-Nachlauf. End-, Fehler- und Cancel-Resultate tragen das exakte Viewerziel; auch ein vor Start verworfener Auftrag wird über seine getrackte Taskidentität an den Owner zurückgeführt. Mehrere ausdrücklich geöffnete Viewer derselben R/O-Quelle bleiben als unabhängige Ansichten zulässig. | Isolierte Selbsttests bestätigten kontinuierlichen Journal-Input mit vollständigem Cancel, ausbleibende Kind- und Nachkommenprozesse, endliches Journal-Ende mit Exitcode 7 sowie Live-File-Append und Cancel. `make clean all CXX=clang++` vollständig ohne Compilerwarnungen. Maintainer-Sichtprüfung bestätigte Resultatrouting und Terminierung; ein vermeintlicher Restworker wurde per Debugger eindeutig einem zweiten weiterhin offenen `JOURNAL: kernel`-Viewer zugeordnet. Abgenommen; keine Regressionchecks ausgeführt. |
| 2026-07-22 | Unterzug 9, Workspace Restore und Owner-Neuerzeugung (`WS-001`) | Workspace und Bento 1.5 serialisieren Fenster-, Pane- und fachlichen Zustand, aber keine Worker-, Task-, Thread-, Core- oder Owner-Laufzeitidentitäten. Restaurierte Editoren und Panes erhalten neue buffergebundene Owner; deren IDX-, WIDTH-, SYN-, FOLD-, MAP- und Pane-Aufträge entstehen aus dem restaurierten Fachzustand neu und werden erneut per globalem Worker-Ordinal modulo verteilt. | Maintainer bestätigte Restore einschließlich minimiertem Zustand, mehrere restaurierte Editor-Owner sowie gleichzeitig sichtbare MAP- und SYN-Arbeit unterschiedlicher `ED`-Owner. Nach hastigem Schließen wurden die verbleibenden Worker vollständig abgebaut. Damit sind weder restaurierte Laufzeit-IDs noch verwaiste Worker oder ein Fokus-Gate beobachtet worden. Kein Laufzeitcode geändert; keine Regressionchecks ausgeführt. |
| 2026-07-22 | Bento-Derived-Projektionen (`BENTO-001`) | Endliche `BDIAG`-/`BOUT`-Worker mit exaktem Pane-Owner, koaleszierter jüngster Anforderung, immutable Projektionstexten und Markerbereichen, kooperativer Outline-Cancellation sowie vollständig versionsgebundener Adoption | Clean-Binary-PTY-Selbsttest: Problems aus realem Compileroutput, 11 Structure- und 31 Functions-Einträge; Problems-Navigation, Pane-Ownership, Restore und Worker-Abbau maintainer-seitig abgenommen; keine Regressionchecks ausgeführt. |
| 2026-07-22 | Bento-Derived-Abschlusskorrekturen (`BENTO-002` bis `BENTO-004`) | Build-/Restart-gebundener Source-Snapshot, genau eine terminale Parse-Anforderung, versionsgebundene Forward-Remaps, vorgemerkte F8-Navigation, Hash-No-op-Adoption und gemeinsame Outline-Snapshot-Basis; unprotokollierte Source-Commits invalidieren Diagnosen und Marker; vollständige Outline-Akquisition ist auf 20.000 Zeilen und 8 MiB begrenzt | Read-only-Abschlussaudit ohne weiteren Funktionsblocker; frischer Start, fünf Restore-Wiederholungen und finaler Restore mit stabilem `Functions [31 items, full]` bestanden. Temporärer Deep-`ReadSnapshot`-Copy-Churn und partielle Outline oberhalb der Grenzen sind akzeptierte Ressourcen- und Supportgrenzen, keine offenen Abnahmen; keine Regressionchecks ausgeführt. |
| 2026-07-22 | Bento-Diagnoseadoption (`BENTO-005`) | Die bestehende Adoptionssperre umfasst nun Text, Marker, Status und Redraw; reentrant ausgelöste `cmUpdateTitle`-Broadcasts können während dieses Abschlusses keine neue Diagnoseformatierung einreihen | Isolierter PTY-Lauf mit zwei realen Clang-Diagnosen: F8 vorwärts, F7 und Shift-F8 rückwärts sowie Mausklick auf beide Problems-Zeilen sprangen exakt; nach der Adoption entstanden in einem zweisekündigen `clone`/`clone3`-Trace keine weiteren Worker. `make clean all CXX=clang++` lief ohne Compilerwarnungen; der F-Tasten- und Churn-Nachweis wurde am Clean-Binary wiederholt. Maintainer-Sichtprüfung bestanden; keine Regressionchecks ausgeführt. |
| 2026-07-22 | Clean-Build-Abortdiagnose (`BUILD-001`) | Der Heap-Abort beim ersten `MiniMapWarmup`-`LaneState` war nur die Detektionsstelle einer zuvor verknüpften ABI-inkonsistenten Objektmenge. Mehrere Bento-Translation-Units besitzen im manuell gepflegten Makefile keine vollständige Abhängigkeit von `MRBentoBox.hpp`; eine Layoutänderung konnte deshalb einen unvollständigen inkrementellen Rebuild erzeugen. | ASan-Clean-Build sowie normaler Clean-Build starteten und restaurierten wiederholt ohne Fehler. Bis zur gesonderten Entscheidung über das geschützte Buildmodell sind nach Header-Layoutänderungen ausschließlich Clean-Builds belastbar; keine Buildmodelländerung und keine Regressionchecks ausgeführt. |
| 2026-07-22 | File-Compare-Pipeline (`FCMP-001`) | Pro Generation zwei pane-eigene Snapshot-Akquisitionen, ein geordneter Myers-Diff und zwei pane-eigene immutable Projektionen; exakte Generations-, Owner-, Dokument- und Versionsadoption; Mutation cancelt den alten Zustand und erzeugt eine neue endliche Fünfergeneration | Isolierter PTY-Selbsttest mit Navigation, beiden Apply-Richtungen, Mutation und Workspace-Restore bestanden; identischer 954-MiB-Vergleich blieb responsiv und schloss ohne Restworker. Initialpalette erscheint bereits im `[comparing]`-Zwischenframe korrekt. Vollständig maintainer-seitig abgenommen; keine Regressionchecks ausgeführt. |
| 2026-07-22 | Abschluss des Gesamtzugs | Abnahmen, Scope, Vergleichsmessungen, Churn-Bewertung und Einschränkungsregister konsolidiert; MFS und pauschales I/O als nicht beauftragte Bestandsbeobachtungen abgegrenzt | `make clean all CXX=clang++` vollständig mit Exitcode 0 und ohne Compilerwarnungen; keine Regressionchecks ausgeführt; Gesamtzug beendet. |

Weitere Einträge nennen den zugehörigen Commit, die betroffenen Sources, den Messdatensatz und das Ergebnis des jeweiligen Prüfziels. Nicht nachgewiesene Behauptungen werden nicht als Umsetzung eingetragen.

## 8. Einschränkungs- und Abweichungsregister

| ID | Bereich | Einschränkung | Entscheidung | Status |
|---|---|---|---|---|
| SYN-001 | Syntax | Verlässlicher Zustand kann nur vorwärts ab bestätigtem Checkpoint entstehen | geordnete Adoption statt Single Worker akzeptiert | verbindlich |
| FOLD-001 | Folding | Strukturzustand kann nur vorwärts ab bestätigtem Checkpoint entstehen | geordnete Validierung statt Single Worker akzeptiert | verbindlich |
| EXT-001 | externe Quelle | Wirklich blockierende Quelle darf ihren exklusiven Worker bis Close/EOF behalten | technisch begründet; nichtblockierende Journal-Pipe, begrenzte Prozessgruppen-Eskalation, exaktes Viewer-Routing und vollständige Terminierung umgesetzt | behoben; Selbsttest, Debuggernachweis und Maintainer-Sichtprüfung bestanden |
| UI-001 | TVision | Zeichnen, Events und begrenzte Adoption bleiben UI-affin | technisch zwingend | verbindlich |
| CPP-001 | Worker-Substrat | Experimenteller Code verwendete `std::jthread` und `std::stop_token` | durch `std::thread`, direkten Stop-Zustand, `condition_variable` und expliziten Join ersetzt | behoben; Harness bestanden |
| HEX-001 | HexBento | String-Erkennung, Inspector-Konvertierung und numerische Formatierung liefen in `draw()` | durch pane-eigene immutable Worker-Projektion ersetzt | behoben; Pane-Owner, Projektion und Interaktion sichtgeprüft |
| HEX-002 | HexBento | Ein sofortiger Retry desselben fehlgeschlagenen Projektionsschlüssels könnte einen Worker-Loop erzeugen | identischer Schlüssel wird erst nach einer neuen externen Anforderung erneut beauftragt | verbindlich; stabiler Rücklauf auf null sichtgeprüft |
| HEX-003 | HexBento | TVision-Scrollbars exponieren nur einen `int`-Wertebereich, der logische Record-Index verwendet `size_t` | oberhalb `INT_MAX` logischer Records wird kein vollständig proportionaler Scrollbar-Support zugesichert; eine Erweiterung benötigt einen eigenen Auftrag | entschiedene Supportgrenze; kein Abschlussblocker |
| AFF-001 | Core-Affinität | fünf Baseline-Aufrufe mit leerer CPU-Maske scheiterten mit `EINVAL` | erlaubte CPU-IDs einmal ermitteln, pro Worker exakt per Ordinal modulo auswählen und jeden Rückgabecode speichern | behoben; Mehr-Worker-als-Cores-Harness ohne Affinitätsfehler bestanden |
| TEL-001 | Instrumentierung | Worker, Ordinal, Core, Task und Lifecycle waren nicht korrelierbar | Lifecycle-Telemetrie und Panel in Unterzug 1 umgesetzt | abgeschlossen; fachliche Paketfelder aller freigegebenen Pipelines nachgewiesen |
| TEL-002 | Instrumentierung | Ein One-shot-Worker konnte `Running` vor dem außerhalb des Lane-Locks erfassten `Queued` protokollieren; dadurch blieb die Q-Säule trotz leerer realer Queue dauerhaft auf eins | Queue-Publikation und `Queued`-Lifecycle unter demselben Lane-Mutex ordnen; keine Panel-Sonderbehandlung | behoben; Debugger-Selbsttest und Maintainer-Sichtprüfung bestanden |
| OWN-001 | Execution Owner | Workergebundene Laufzeit-ID gruppierte mehrere Worker desselben fachlichen Owners nicht | explizites Paar aus Owner-Art und ownerlokaler ID an jeder Submission; keine Persistenz von Worker-Zustand | behoben; Workspace-, Editor- und Pane-Nachweis bestanden |
| ID-001 | Identitätsmodell | `workerId` und `workerOrdinal` zählten dasselbe Erzeugungsereignis getrennt; Owner-ID lief wegen Lane-Bindung häufig numerisch parallel | `workerOrdinal` als einzigen Worker-Bezeichner durchgesetzt; Owner, Task, Plattformthread, Core und Summen sprachlich und strukturell getrennt | behoben; statisch und dynamisch geprüft |
| PKT-001 | Paketmetadaten | Generation, BOF/EOF und Span sind instrumentierbar; Display-Width-Aufträge liefern erstmals kanonische Werte, übrige Aufgaben noch nicht | ausschließlich aus den jeweiligen fachlichen Paketbildnern befüllen | für IDX, WIDTH, SYN, FOLD, MAP, HEX, BDIAG und BOUT nachgewiesen |
| CHURN-001 | Worker-Lebensdauer | Kernprüflauf erzeugte 962 Affinitätsaufrufe bei maximal 30 gleichzeitig beobachteten Threads | fachlichen Result-Churn, Restworker, Systemlast und Interaktionslatenz statt die gewollte absolute One-shot-Zahl bewerten | abgeschlossen; SYN und WIDTH quantitativ reduziert, MAP begrenzt, vollständiger Rücklauf und performante Dauernutzung bestätigt |
| COAL-001 | Submission | One-shot-`submitCoalesced()` besaß keine gemeinsame Queue für ältere Schlüssel | tote API und zugehörigen Lifecycle-Grund entfernt | behoben |
| LINE-001 | Line-Index | Draw und Metrics können über `lineCount()` den vollständigen Scan synchron erzwingen | in BOF-/EOF-Paketpfad überführen | behoben; enwik9-Restore und Interaktionsverhalten sichtgeprüft |
| WIDTH-001 | Display-Breite | eine außergewöhnlich lange Einzelzeile wird wegen Tab-/Unicode-Vorwärtszustand innerhalb eines Workers sequenziell ausgewertet | UI bleibt frei; Abbruch erfolgt spätestens an der nächsten Zeilengrenze | technisch begrenzt |
| WIDTH-002 | Display-Breite | kernweise Zeilenpakete benötigen den exakten Line-Index | Width-Generation erst nach dessen Adoption starten; keine geschätzten Zeilenadressen verwenden | verbindlich |
| WIDTH-003 | Display-Breite | der sequenzielle Paketlauf adressierte jede Zeile erneut per `lineStartByIndex()` und wiederholte dadurch bis zu 4.095 Zeilenübergänge je Abfrage | Paketstart einmal adressieren und anschließend linear mit `nextLine()` fortschreiten; finalen enwik9-Vollscan messen | korrigiert; Vollscan und Interaktionsverhalten abgenommen |
| LINE-002 | Textbearbeitung | die Line-Drawing-Materialisierung adressierte einen zusammenhängenden Zeilenbereich in jeder Iteration erneut per `lineStartByIndex()` | einmalige Startadressierung und lineare Offsetfortschreibung verwenden | behoben; Editor-Selbsttest und Maintainer-Sichtprüfung bestanden |
| LEDGER-001 | Line-Index | nur BOF-Präfix statt beliebiger verlässlich warmer Spans | domänenspezifischen Line-Ledger einführen | behoben; Fernsprung- und Hintergrundfensterverhalten sichtgeprüft |
| SYN-002 | Syntax | ein persistenter Worker serialisiert Generationen eines Editors | Checkpoint-Ketten mit mehreren endlichen Workern | behoben; Adoption, Churn und Interaktionsverhalten sichtgeprüft |
| FOLD-002 | Folding | Eine gewöhnliche Fernsprung-Viewportgeneration darf nicht mit leerem Strukturzustand an einem beliebigen Scanfensterrand beginnen | dokumentweiten BOF-Checkpointledger, parallele Textakquisition, geordnete Vorwärtsvalidierung und einen exakten Kontext-Bridge bis zum Viewportanker verwenden | behoben; PTY-Selbsttest und Maintainer-Sichtprüfung bestanden |
| MINI-001 | Minimap | ein persistenter Worker, Draw-getriebene Submission und nur ein Cachefenster | unabhängige Pakete, laufende Spanreservierungen und Warmfenster-Ledger je Editor | behoben; Churn- und Queue-Sichtprüfung bestanden |
| MINI-002 | Minimap | Range-Signaturen und Overlay-Projektion laufen unbudgetiert in `draw()` | in Worker-Projektion aufnehmen | behoben; `draw()` ist reiner Projektionskonsument und die Darstellung sichtgeprüft |
| BENTO-001 | Bento | Diagnostik- und Outline-Projektionen liefen als Vollarbeit im UI-Thread | Pane-Owner und budgetierte Adoption | behoben; Clean-Binary-Selbsttest und wiederholte Maintainer-Sichtprüfung bestanden |
| BENTO-002 | Bento-Diagnostik | `ReadSnapshot` kann einen bereits materialisierten Text und Line-Index-Metadaten tief kopieren; eine Folge beobachteter Source-Edits hält alte und neue Snapshots bis zum Forward-Remap vorübergehend gleichzeitig | keine zusätzliche Snapshot-Registry einführen; temporären Copy- und RAM-Churn in diesem Korrekturzug als nichtfunktionales Restrisiko akzeptieren | dokumentiert; kein Abschlussblocker, erneute Messung nur bei sichtbarer Latenz oder dauerhaftem Wachstum |
| BENTO-003 | Bento-Diagnostik | Ein unprotokollierter Source-Commit besitzt keine lückenlose `DocumentChangeSet`-Kette und erlaubt deshalb kein verlässliches Remap alter Compilerpositionen | Diagnosen und Marker verwerfen, Quelle invalidieren und erst mit einem normalen Build oder Restart neu baselinen | akzeptierte Supportgrenze; kein Abschlussblocker |
| BENTO-004 | Bento-Outline | Eine vollständige Fold-Akquisition ist auf Dokumente mit höchstens 20.000 Zeilen und höchstens 8 MiB begrenzt | innerhalb beider Grenzen vollständige Akquisition auf gemeinsamer Snapshot-Basis; oberhalb einer Grenze nur bestätigte sichtbare Fold-Spans als partielle Outline projizieren | akzeptierte Ressourcenbegrenzung; kein Abschlussblocker |
| BENTO-005 | Bento-Diagnostik | Markerübernahme und Redraw konnten während einer Diagnoseadoption über Minimap und `cmUpdateTitle` reentrant einen weiteren Formatierungstask anfordern | die bestehende Adoptionssperre über den gesamten fachlichen Abschluss halten; keine Keymap- oder Navigationsänderung | behoben; fokussierter PTY-Selbsttest und Maintainer-Sichtprüfung bestanden |
| BUILD-001 | Buildmodell | Die manuell gepflegten Headerabhängigkeiten der Bento-Translation-Units sind unvollständig; eine Layoutänderung in `MRBentoBox.hpp` kann deshalb bei einem inkrementellen Build ABI-inkonsistente Objektdateien verknüpfen | bis zur gesonderten Freigabe einer Änderung des geschützten Buildmodells nach Header-Layoutänderungen immer `make clean all CXX=clang++` verwenden | entschiedene Betriebsgrenze; geschütztes Buildmodell bleibt außerhalb dieses Zugs |
| FCMP-001 | File Compare | Snapshot-Split vor Submission und vollständige Resultatprojektion nach Completion laufen im UI-Thread | zwei pane-eigene Akquisitionsworker, ein geordneter Myers-Worker und zwei pane-eigene immutable Projektionsworker; UI übernimmt nur versionierte Komplettresultate | behoben und vollständig abgenommen, einschließlich Initialpalette im `[comparing]`-Zwischenframe |
| MFS-001 | Multi-File-Search | Verzeichnislauf, Datei-I/O und Regex-Matching laufen seriell im UI-Thread | bei unzumutbarem realem Modalverhalten einen eigenen, messwertbasierten Zug eröffnen | Bestandsbeobachtung; nicht beauftragt und kein Abschlussblocker |
| IO-001 | Datei- und Prozess-I/O | mehrere Load-, Save-, Dialog- und MRMAC-External-I/O-Pfade können den UI-Thread blockieren | nur einen konkret gemessenen Nutzungsbefund klassifizieren und nach eigenem Plan ändern; kein pauschaler Auslagerungsauftrag | Bestandsbeobachtung; nicht beauftragt und kein Abschlussblocker |
| NAV-001 | Goto line | `handleSearchGotoLineNumber()` lief bei kaltem Großdokument synchron ab BOF über `nextLineOffset()` bis zur Zielzeile und blockierte den UI-Thread beim Sprung zu Zeile 13.000.000 etwa fünf Sekunden | bestätigten Line-Index beziehungsweise dessen Deferred-Ledger verwenden; bis zur Adoption keine lineare UI-Thread-Schleife ausführen | behoben; isolierter PTY-Selbsttest und Maintainer-Freigabe bestanden |
| DEAD-001 | Task-Arten | `SaveNormalizationWarmup` und `IndicatorBlink` besaßen Dispatch-Reste, aber keine Submission | fachliche Notwendigkeit entscheiden und tote API beseitigen | behoben; Selbsttest und Maintainer-Sichtprüfung bestanden |
| WS-001 | Workspace Restore | keine persistierten Worker-IDs, aber auch keine beweisbare neue Owner-/Worker-Zuordnung | nach Owner-Instrumentierung in Unterzug 9 prüfen | behoben; neue Owner, parallele Hintergrundarbeit und vollständiger Abbau sichtgeprüft |

Neue Einschränkungen werden vor ihrer Implementierung hier mit technischer Ursache, Auswirkung und Maintainer-Entscheidung eingetragen. Bequemlichkeit, bestehende Lane-Struktur oder Fokuszustand sind keine zulässigen technischen Ursachen. Innerhalb des freigegebenen Korrekturscopes enthält das Register zum Abschluss keinen offenen Implementierungs- oder Abnahmepunkt; als Bestandsbeobachtung beziehungsweise Supportgrenze entschiedene Einträge eröffnen keinen Folgezug ohne neuen Maintainer-Auftrag.

## 9. Abschlusskriterien

Der Korrekturzug ist erst abgeschlossen, wenn:

1. sämtliche bekannten Scan-, Warm-up-, Derived-, Bento-, Prozess- und Stream-Pfade der Bestandsmatrix zugeordnet sind,
2. kein Owner wegen einer Lane-weiten Single-Worker-Struktur auf einen anderen Owner wartet,
3. Core-Zuordnung und Worker-Anzahl der strikten Modulo-Regel entsprechen,
4. Hintergrundfenster und Hintergrund-Panes nachweislich weiterarbeiten,
5. Syntax und Folding kanonisch korrekte Ergebnisse mit geordneter Adoption liefern,
6. Workspace Restore korrekte Owner-Lebenszyklen erzeugt,
7. das Performance Panel alle geforderten Lifecycle-Ereignisse zeigt,
8. Baseline und Abschlussmessungen reproduzierbar protokolliert sind,
9. sämtliche offenen Einträge des Einschränkungsregisters entschieden oder beseitigt sind,
10. `make clean all CXX=clang++` ohne neue Warnungen erfolgreich ist.

Die Kriterien 1 bis 9 sind mit den Einzelabnahmen und der konsolidierten Bewertung in Abschnitt 5.17 erfüllt. Sie beziehen sich auf den freigegebenen Funktionsscope, nicht auf jeden während der Bestandsaufnahme vorsorglich genannten synchronen Anwendungspfad. Kriterium 10 wurde durch den abschließenden Clean-Build mit Exitcode 0 und ohne Compilerwarnungen erfüllt. Der Korrekturzug ist beendet.
