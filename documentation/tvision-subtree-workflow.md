# TVision Subtree Workflow

Stand: 2026-04-02

## Ziel

`mr` fuehrt `tvision/` als Upstream-nahen Quellbaum mit klarer Patch-Queue:

- Upstream-Quelle: `magiblot/tvision`
- Lokaler Prefix: `tvision/`
- Lokale Abweichungen als kleine Patches in `patches/`

## Verbindliche Regeln

1. Keine direkten, ad-hoc Aenderungen in `tvision/source` oder `tvision/include` ohne korrespondierende Patch-Datei.
2. Nur `tvision/include/**` und `tvision/source/**` zaehlen als TVision-Source-Wahrheit.
3. Build-Artefakte (`tvision/build*`) sind niemals Upstream-Diff-Grundlage.
4. Patches werden einheitlich ueber `make tvision-apply-patches` eingespielt.

## Patch-Queue

Aktuell:

- `patches/0001-tvision-guard-command-range.patch`
- `patches/0002-tvision-codepage-include-tv-h.patch`
- `patches/0003-tvision-colorsel-clamp-group-index.patch`

## Standardbefehle

### Alles in einem Schritt (empfohlen)

```bash
make tvision-sync-safe
```

Der Ablauf ist: Workspace sichern (stash), Upstream fetchen, Subtree pullen, lokale Patches anwenden, Workspace zurueckholen.

### Upstream beziehen

```bash
make tvision-upstream-fetch
```

### Subtree-Update einziehen

```bash
make tvision-subtree-pull
```

### Lokale Patches anwenden

```bash
make tvision-apply-patches
```

### Status anzeigen

```bash
make tvision-status
```

### Build

```bash
make -j4 mr
```

## Hinweise

- `tvision-subtree-pull` erwartet einen sauberen Git-Status.
- Falls der Arbeitsbaum belegt ist, zuerst sichern/stashen und danach Pull + Patch anwenden.
- `.vendor-cache` wird in diesem Modell nicht mehr als Pflichtbestandteil verwendet.
- Build-Artefakte unter `tvision/build` bleiben lokal und sind kein Teil der Patch-Queue.
