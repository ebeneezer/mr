Architekturkarte

1. App / UI / Dialoge

Zentrale Dateien: ￼app/MREditorApp.cpp, ￼app/MRCommandRouter.cpp, ￼app/MRMenuFactory.cpp, ￼dialogs/MRSetup.cpp, ￼dialogs/MRWindowList.cpp, ￼dialogs/MRKeymapManager.cpp, ￼dialogs/MRMacroFile.cpp.
Zentrale Klassen/Funktionen: MREditorApp, MRCommandRouter, MRDialogFoundation, MRScrollableDialog, die großen Dialogklassen pro Datei.
Autoritativer Zustand: UI selbst ist nicht autoritativ für Settings; Dialoge halten nur Eingabepuffer, Auswahlzustände und TVision-Views. Autoritativer Laufzeitzustand liegt meist in config/MRDialogPaths.cpp, Window-/Editorzustand in App/UI-Klassen.
Wichtige Datenflüsse: Benutzerereignis -> TVision Event -> MREditorApp/MRCommandRouter -> Dialog/Command -> Settings-/Window-/VM-Pfade.
Kritische Seiteneffekte: Dialoge triggern Persistenz, History-Updates, Keymap-Laden, Workspace-Operationen und Message-Line-Meldungen.
Schnittstellen: zu TVision, Settings-Modell, Window-Management, Keymap, VM und Coprocessor.
AGENTS-Grenzen: keine neue Dialog-Architektur, keine künstlichen Mini-Helper, keine UI-Hacks außerhalb TVision-Nähe.
Nicht opportunistisch ändern: Setup-Dialoge, Dirty-Gating, History-Verhalten, Help-/File-Dialog-Pfade.
Bekannte technische Schulden: einige große Dialoge bündeln viel Verantwortung; Keymap-Dialog enthält lokale Diagnostikformatierung parallel zum Loader; MRSetup.cpp bleibt zentrale Sammeldatei für Dialog-Grundklassen und Setup-Logik.
2. TVision-Integration

Zentrale Dateien: ￼dialogs/MRSetupCommon.hpp, ￼dialogs/MRSetup.cpp, ￼ui/MRFrame.cpp, ￼ui/MRWindowSupport.cpp, ￼ui/MRMessageLineController.cpp, ￼ui/MRColumnListView.cpp.
Zentrale Klassen/Funktionen: MRScrollableDialog, MRDialogFoundation, MRFrame, Message-Line-Posting, dialognahe File-/Window-Support-Funktionen.
Autoritativer Zustand: TVision hält Fokus, Z-Order, Event-Routing und View-Lifetime; MR ergänzt dialognahe Scroll-/Validation-/Managed-View-Logik.
Wichtige Datenflüsse: Domainzustand -> TVision-Views -> Draw/HandleEvent; Message-Line als UI-Rückkanal für Fehler/Warnungen.
Kritische Seiteneffekte: insert, addManaged, Scrollprojektion, Done-Button-Ghosting, Fokussteuerung.
Schnittstellen: App-Schicht, Dialoge, MacroCellView, Message-Line.
AGENTS-Grenzen: keine direkten Screen-Buffer-Hacks, keine Overlay-Abkürzungen, keine Änderungen unter tvision/.
Nicht opportunistisch ändern: Draw-/Event-Mechanik, MRDialogFoundation-Layoutverhalten, Message-Line-Owner-Semantik.
Bekannte technische Schulden: zentrale TVision-Hilfen sitzen teils in Setup-Dateien; manche wiederverwendbaren UI-Bausteine sind groß und gemischt, aber aktuell stabil.
3. Settings / settings.mrmac / MRSETUP

Zentrale Dateien: ￼config/MRDialogPaths.cpp, ￼config/MRDialogPaths.hpp, ￼config/MRSettingsLoader.cpp, ￼app/MREditorApp.cpp, ￼app/commands/MRWindowCommands.cpp.
Zentrale Klassen/Funktionen: MRSetupPaths, applyConfiguredSettingsAssignment, applyConfiguredEditSetupValue, applyConfiguredColorSetupValue, persistConfiguredSettingsSnapshot, writeSettingsMacroFile, parseSettingsDocument.
Autoritativer Zustand: zentraler In-Memory-Settingszustand in config/MRDialogPaths.cpp; settings.mrmac ist nur Serialisierung dieses Zustands.
Wichtige Datenflüsse: settings.mrmac/source -> MRSettingsLoader -> applyConfigured*-> zentraler Store; umgekehrt Store -> buildSettingsMacroSource* -> Datei.
Kritische Seiteneffekte: Dirty-Markierung, Dialog-History, Theme-Datei, Keymap-Profile, Autoexec-Makros, Workspace-Beimischung.
Schnittstellen: VM-Startup, App-Bootstrap, Dialoge, WindowCommands, Keymap, Theme.
AGENTS-Grenzen: ein autoritatives Laufzeitmodell, keine Shadow-Stores, keine Save/Reload-Workarounds.
Nicht opportunistisch ändern: Settings-Laufzeitmodell, settings.mrmac-Semantik, MRSETUP-Key-Bedeutung, Persistenzpfade.
Bekannte technische Schulden: Startup-Flow gesplittet, Persistenzpfade doppelt, Workspace-Serialisierung separat eingemischt, VM greift in Settings-Anwendung/Persistenz ein.
4. mrmac Lexer / Parser / Compiler

Zentrale Dateien: ￼mrmac/lexer.l, ￼mrmac/parser.y, ￼mrmac/mrmac.c, ￼mrmac/MRMacroRunner.cpp.
Zentrale Klassen/Funktionen: Flex/Bison-Scanner und Parser, Bytecode-Erzeugung, Macro-Runner für Vorder-/Hintergrundmakros.
Autoritativer Zustand: geparste Makroquelle und erzeugter Bytecode; keine Settings-Autorität.
Wichtige Datenflüsse: Datei/Text -> Lexer -> Parser -> Bytecode -> VM/Runner.
Kritische Seiteneffekte: Makroaufrufe können UI, TVCALL, Settings-Startup und Background-Jobs auslösen.
Schnittstellen: VM, App, Coprocessor, Dateisystem.
AGENTS-Grenzen: keine opportunistischen Sprachsemantik-Änderungen, keine ungeklärten Parser-/Compiler-Abkürzungen.
Nicht opportunistisch ändern: Grammar, Bytecode-OpCodes, Startup-Kontext für MRSETUP.
Bekannte technische Schulden: generierte Artefakte und historische Makro-Semantik sind stabilitätskritisch; Änderungen brauchen Regression-Abdeckung.
5. VM / Intrinsics / TVCALL

Zentrale Dateien: ￼mrmac/MRVM.cpp, ￼mrmac/MRVM.hpp.
Zentrale Klassen/Funktionen: VM-Ausführung, Intrinsic-Dispatch, OP_TVCALL, MacroCellGrid, MacroCellView, UiScreenStateFacade, mrvmUiRenderFacadeRenderDeferredCommand.
Autoritativer Zustand: VM-Execution-State, UI-Facade-Zustand für Macro-Screen-Overlay, Runtime-Umgebung.
Wichtige Datenflüsse: Bytecode -> VM -> Intrinsics/TVCALL -> UI-Facade oder Deferred-Command-Erzeugung.
Kritische Seiteneffekte: Bildschirmprojektion, Dialoge aus Makros, Settings-Anwendung im Startup-Modus, SAVE_SETTINGS, Keymap-Payload-Anwendung.
Schnittstellen: Settings-Modul, TVision-nahe UI, Coprocessor-Dispatch, App.
AGENTS-Grenzen: VM-Ausführung, MRSETUP-Semantik, TVCALL-Fluss, MacroCellGrid/MacroCellView nicht opportunistisch ändern.
Nicht opportunistisch ändern: Startup-Gating für MRSETUP, direkte VM-Persistenzpfade, Overlay-/Projection-Logik.
Bekannte technische Schulden: VM kennt Settings- und Persistenzoperationen direkt; High-Risk-Grenze zwischen Producer und Consumer ist empfindlich.
6. Keymap

Zentrale Dateien: ￼keymap/MRKeymapProfile.hpp, ￼keymap/MRKeymapProfile.cpp, ￼keymap/MRKeymapResolver.cpp, ￼keymap/MRKeymapTrie.cpp, ￼keymap/MRKeymapActionCatalog.cpp, ￼dialogs/MRKeymapManager.cpp.
Zentrale Klassen/Funktionen: MRKeymapProfile, MRKeymapDiagnostic, loadKeymapProfilesFromSettingsSource, canonicalizeKeymapProfiles, MRKeymapResolver, MRKeymapTrie, runtimeKeymapResolver.
Autoritativer Zustand: konfigurierte Keymap-Profile plus aktives Profil im Settings-Modell; Resolver ist Laufzeitprojektion darauf.
Wichtige Datenflüsse: Settings-Quelle -> Keymap-Parser/Kanonisierung -> Settings-Modell -> Resolver -> Runtime-Keymap-Handling.
Kritische Seiteneffekte: Diagnostics, Fallback auf DEFAULT, Persistenz ins Settings-Source-Format, Dialog-Load/Save.
Schnittstellen: Settings-Loader, Dialogmanager, Runtime-Keymap-Event-Handling.
AGENTS-Grenzen: keine Änderung an Ladeverhalten, Persistenzformat, Resolver-Semantik oder Diagnostiktexten ohne Begründung.
Nicht opportunistisch ändern: aktive Profilwahl, DEFAULT-Fallback, Keymap-Payload-Keys.
Bekannte technische Schulden: Diagnostikformatierung doppelt im Loader und Dialog, aber nicht sauber konsolidierbar ohne API-Bewegung.
7. Coprocessor / Deferred UI / Macro Screen

Zentrale Dateien: ￼coprocessor/MRCoprocessor.hpp, ￼coprocessor/MRCoprocessor.cpp, ￼coprocessor/MRCoprocessorDispatch.cpp, ￼coprocessor/MRPerformance.cpp.
Zentrale Klassen/Funktionen: Coprocessor, Result, TaskInfo, queueDeferredMacroUiPlayback, pumpDeferredMacroUiPlayback, MacroScreenModel, DeferredUiRenderGateway, MacroScreenView.
Autoritativer Zustand: Task-Queues/Lane-State im Coprocessor; für Deferred-Macro-Playback der lokale Playback-Queue- und Screen-Model-Zustand.
Wichtige Datenflüsse: Background-Task -> Result -> Dispatch -> UI/Window/Performance/Deferred-Playback; Macro-Commands -> Deferred-UI-Queue -> VM-UI-Facade-Render.
Kritische Seiteneffekte: Hintergrund-Completion, staged UI commands, batching mit mrvmUiBeginMacroScreenBatch()/End, Performance-Logging.
Schnittstellen: VM, UI, PieceTable/Editor, Message-Line.
AGENTS-Grenzen: keine Änderung an Deferred-UI-Command-Fluss, Thread-Annahmen, Ownership/Lifetime, MacroCellGrid/MacroCellView.
Nicht opportunistisch ändern: Gateway/View-Chain, Screen-Mutation-Epoch-Logik, batch boundaries.
Bekannte technische Schulden: rein mechanische Routing-Typen existieren, sind aber durch Regression-Checks als Strukturvertrag fest verdrahtet.
8. File-/Path-Utilities

Zentrale Dateien: ￼app/utils/MRStringUtils.cpp, ￼app/utils/MRFileIOUtils.cpp, ￼config/MRDialogPaths.cpp, ￼ui/MRWindowSupport.cpp, ￼app/commands/MRExternalCommand.cpp.
Zentrale Klassen/Funktionen: trimAscii, normalizeConfiguredPathInput, makeAbsolutePath, diverse Validatoren/Datei-I/O-Helfer.
Autoritativer Zustand: keiner; dies sind Hilfsroutinen, oft aber mit Seiteneffekten über History oder Settings-Setter gekoppelt.
Wichtige Datenflüsse: Benutzerpfad/Text -> Trimmen/Expandieren/Normalisieren -> Validieren -> Store oder Dateisystem.
Kritische Seiteneffekte: History-Updates, Dirty-Markierung, implizite Pfad-Defaults.
Schnittstellen: Dialoge, Settings, externe Kommandos, File-Dialogs.
AGENTS-Grenzen: keine neue lokale Duplikation für Pfadexpansion, Pfadnormalisierung, File-Checks.
Nicht opportunistisch ändern: Pfad-/History-Semantik, ~-Expansion, Default-/Fallback-Logik.
Bekannte technische Schulden: es existieren weiterhin mehrere lokale Pfadhelfer in unterschiedlichen Dateien; A2 hat gezeigt, dass diese nicht überall sicher durch öffentliche Projektfunktionen ersetzbar sind.
9. Build / Generated Files / Regression Checks

Zentrale Dateien: ￼Makefile, ￼generate_help_markdown.sh, ￼generate_about_quotes.sh, ￼regression/mr-regression-checks.cpp, ￼tree-sitter/Package.swift.
Zentrale Klassen/Funktionen: Build-Regeln, generierte Header app/MRHelp.generated.hpp und app/MRAboutQuotes.generated.hpp, strukturelle Regression-Checks.
Autoritativer Zustand: Makefile ist Build-Orchestrator; Regression-Checks kodieren zusätzliche Strukturverträge über bloßes Kompilieren hinaus.
Wichtige Datenflüsse: Quellen/Skripte -> generierte Header -> Build; Quelltexte -> Regression-Checks -> Strukturfreigabe.
Kritische Seiteneffekte: clean-Verhalten, generierte Dateien, textuelle Strukturprüfungen, Audio-Signale im Makefile.
Schnittstellen: gesamtes Projekt.
AGENTS-Grenzen: realer Clean-Build vor Handoff; paplay-Signale nicht brechen.
Nicht opportunistisch ändern: generierte-Datei-Modell, Regression-Checks, TVision-Build-Pfad.
Bekannte technische Schulden: versionierte generierte Header und Build-Generierung leben im Mischmodell; der Clean-Build-Fix musste dieses Modell ausdrücklich stabilisieren.
A. High-Risk-Knoten

1. Settings Startup Flow

Ist-Zustand: Bootstrap liegt verteilt in MREditorApp, MRSettingsLoader, Settings-Setterpfaden und VM-Startup-Gating.
Warum riskant: mehrere konzeptionelle Apply-/Normalisierungsstellen können als „autoritativer Pfad“ wirken.
Vor jeder Änderung klären: Wer normalisiert wann? Wer lädt? Wer darf Defaults ergänzen? Gibt es genau einen finalen Apply-Pfad?
Tests/Builds: make clean all CXX=clang++, regression/mr-regression-checks, Startup-Probe mit gültiger/ungültiger settings.mrmac, Neustart mit bestehender Datei.
Nicht ohne Freigabe ändern: Bootstrap-Reihenfolge, MRSETUP-Startup-Modus, unbekannte/obsolete Key-Behandlung.
2. settings.mrmac Persistenz

Ist-Zustand: Persistenz liegt in persistConfiguredSettingsSnapshot, writeSettingsMacroFile, Theme-Datei-Schreiben und Workspace-Zusatzpfad.
Warum riskant: mehrere Schreibpfade mit Seiteneffekten auf Theme, History und Dirty-State.
Vor jeder Änderung klären: Gibt es genau einen Writer? Was gehört zur Settings-Datei, was nicht? Ist Theme-Schreiben Teil derselben Transaktion?
Tests/Builds: Clean-Build, Persistenz nach Setup-Änderung, Exit-Persistenz, Save ohne Änderungen, Save mit Theme/Keymap/Workspace.
Nicht ohne Freigabe ändern: Schreibpfade, Dirty-Clear-Logik, Theme-Kopplung, Exit-Persistenz.
3. Workspace-Serialisierung

Ist-Zustand: Workspace wird separat als MRSETUP('WORKSPACE', ...) in die Settings-Quelle eingefügt und separat wieder gelesen.
Warum riskant: paralleler Serialisierungspfad neben dem zentralen Settings-Source-Bau.
Vor jeder Änderung klären: Ist Workspace Bestandteil von settings.mrmac oder nur Beimischung? Wer ist Eigentümer des Formats?
Tests/Builds: Clean-Build, Save/Reload der Workspace-Fenster, Positions-/Cursor-/VD-Wiederherstellung, Verhalten ohne Workspace-Einträge.
Nicht ohne Freigabe ändern: WORKSPACE-Format, Einfügeposition, Lese-/Schreibpfad.
4. MRSETUP / SAVE_SETTINGS in der VM

Ist-Zustand: VM erlaubt MRSETUP nur im Startup-Modus, ruft dann direkt Settings-Anwendung auf; SAVE_SETTINGS persistiert aus der VM heraus.
Warum riskant: VM überschreitet damit die reine Ausführungsgrenze in Settings- und Persistenzlogik.
Vor jeder Änderung klären: Welche Keys dürfen im VM-Pfad bleiben? Ist Persistenz aus der VM Architektur oder Altlast? Welche Fehlerform wird garantiert?
Tests/Builds: Clean-Build, Regression-Checks zu verbotenen/erlaubten MRSETUP-Pfaden, VM-Fehlermeldungen, SAVE_SETTINGS-Erfolg/Fehler.
Nicht ohne Freigabe ändern: Startup-Gating, erlaubte Keys, Fehlertexte, Persistenzaufrufe.
5. TVCALL / MacroCellGrid / MacroCellView

Ist-Zustand: VM produziert Screen-/Overlay-Operationen; MacroCellGrid hält Modell, MacroCellView projiziert TVision-nah.
Warum riskant: Producer-/Consumer-Grenze, Overlay-Rendering und Deferred-Playback hängen eng zusammen.
Vor jeder Änderung klären: Wer ist Producer? Wer projiziert? Gibt es konkurrierende Write-Pfade? Bleibt TVision-Nähe erhalten?
Tests/Builds: Clean-Build, Regression-Checks zu whitelisted mrvmUi*-Bridges, TVCALL-Makros, Overlay-/Message-/Marquee-Fälle, Deferred-Playback.
Nicht ohne Freigabe ändern: MacroCellGrid, MacroCellView, TVCALL-Routing, batch boundaries, direct render facade usage.
6. Keymap-Diagnostik und Keymap-Persistenz

Ist-Zustand: Keymap wird aus Settings-Source geparst, kanonisiert, in Settings-Zustand geschrieben und zur Laufzeit im Resolver projiziert; Diagnostikformatierung ist im Loader und Dialog doppelt.
Warum riskant: Persistenzformat, DEFAULT-Fallback, Resolver-Rebuild und UI-Diagnostik hängen zusammen.
Vor jeder Änderung klären: Wer besitzt das Keymap-Source-Format? Wer darf canonicalisieren? Sind Diagnostiktexte Teil der UI-Verträge?
Tests/Builds: Clean-Build, Settings-Bootstrap mit fehlerhaften/duplizierten Keymaps, Dialog-Load/Save, Resolver-Rebuild, aktive Profilumschaltung.
Nicht ohne Freigabe ändern: Payload-Keys, DEFAULT-Fallback, Diagnostiktexte, Loader/Dialog-Rollen.
B. Empfohlene nächste echte Projektentscheidungen

Settings-Bootstrap-Vertrag
Optionen: heutigen Mischzustand nur dokumentieren; oder einen eindeutigen Bootstrap-Vertrag festlegen.
Empfehlung: zuerst den Vertrag festlegen, nicht sofort umbauen.
Kernfrage: Welcher Pfad ist der einzige autoritative Startup-Apply-Pfad?
Persistenz-Vertrag für settings.mrmac, Theme und Workspace
Optionen: alles bewusst in einem Schreibvorgang definieren; oder Workspace/Theme explizit als getrennte Artefakte behandeln.
Empfehlung: Entscheidung schriftlich vor jeder Codeänderung.
Kernfrage: Was ist Teil des Settings-Snapshots, was nur angrenzender Zustand?
VM-Grenze zu Settings/Persistenz
Optionen: VM darf Startup-Settings anwenden und auch persistieren; oder VM darf nur Startup-Settings anwenden, Persistenz bleibt App-Schicht.
Empfehlung: diese Grenze explizit entscheiden, bevor an MRSETUP oder SAVE_SETTINGS gearbeitet wird.
Kernfrage: Ist VM hier Producer oder Orchestrator?
Keymap-Besitzmodell
Optionen: Loader ist Eigentümer des Settings-Formats und Dialog bleibt nur Editor; oder Dialog und Loader teilen bewusst Formatlogik.
Empfehlung: Loader/Settings als Format-Eigentümer festlegen, Dialog als Consumer/Editor belassen.
Kernfrage: Wo liegt die kanonische Text- und Diagnostiksemantik?
Deferred-UI-Playback-Vertrag
Optionen: bestehende Gateway/View-Chain als Strukturvertrag beibehalten; oder später bewusst reduzieren und die Regression-Checks dazu anpassen.
Empfehlung: vorerst als Strukturvertrag behandeln.
Kernfrage: Sind diese Pfade nur Implementierungsdetail oder bewusst abgesicherte Architekturgrenze?
Build- und Regression-Philosophie
Optionen: Regression-Checks bleiben auch textuell-strukturell; oder sie werden langfristig stärker verhaltensorientiert.
Empfehlung: kurzfristig unverändert lassen, langfristig bewusst entscheiden.
Kernfrage: Welche Strukturteile sollen absichtlich nicht „wegrefaktoriert“ werden dürfen?
C. Stop-Liste

Settings-Startup-Flow
settings.mrmac-Persistenzpfade
Workspace-Serialisierung
MRSETUP-/SAVE_SETTINGS-Pfad in der VM
TVCALL-Rendering, MacroCellGrid, MacroCellView, Deferred-UI-Bridge
Keymap-Persistenzformat und aktive Profil-Fallbacks
Build-Modell für generierte Header jenseits des bereits reparierten Clean-Build-Fixes
Dialog-History-/Path-History-Semantik
große Konsolidierungen über Modulgrenzen nur wegen lokaler Duplikation
Die Mikro-Kalibrierung hat damit einen brauchbaren Zustand erreicht: kleine lokale Korrekturen funktionieren, aber die nächsten sinnvollen Schritte sind Architekturentscheidungen, nicht weitere Kleinstbereinigungen.
