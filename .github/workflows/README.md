# Forge fuer R36S (ArkOS) - Stufe 1

Ein kleines 3D-Spiel im Stil von The Forge (eigene Modelle, eigene Welt):
Erze abbauen, im Ofen schmelzen, am Amboss schmieden (Timing-Minispiel), beim Haendler verkaufen und die Spitzhacke verbessern.

## So kommt es auf deinen R36S

1. Auf GitHub ein neues Repository anlegen (z. B. `forge-r36s`).
2. Alle Dateien dieses Ordners hochladen (Add file > Upload files).
   Wichtig: Der Ordner `.github/workflows/build.yml` muss mit hochgeladen werden.
   Falls er fehlt: Add file > Create new file, als Namen `.github/workflows/build.yml` eintippen und den Inhalt aus der Datei einfuegen.
3. Oben auf den Tab **Actions** gehen. Der Build startet von selbst (dauert ca. 5-10 Minuten).
   Falls nicht: Actions > "Build R36S" > Run workflow.
4. Wenn der Build gruen ist: auf den Lauf klicken, unten bei **Artifacts** `Forge-R36S` herunterladen und entpacken.
5. Auf die SD-Karte kopieren: `Forge.sh` und den Ordner `forge` nach `roms/ports/` (bzw. `roms2/ports/`).
6. Auf dem R36S: Ports > Forge starten.

## Steuerung

| Aktion | Taste |
|---|---|
| Laufen | linker Stick / Steuerkreuz |
| Kamera drehen | rechter Stick |
| Abbauen / Benutzen / Auswaehlen | A |
| Springen / Zurueck | B |
| Beenden | Start + Select |

## Spielablauf

- Erze liegen in Ringen um das Dorf: Kupfer (nah), Eisen, Gold, Kristall (weit weg).
- Ofen (links im Dorf): A druecken, alle Erze werden zu Barren geschmolzen.
- Amboss (Mitte): Waffe oder Ruestung waehlen, dann mit A im richtigen Moment hammern. Je naeher an der Mitte, desto besser die Qualitaet (= mehr Wert).
- Haendler (rechts): Erze, Barren und Waffen verkaufen, Spitzhacke verbessern.
- Die beste geschmiedete Ruestung traegst du automatisch (Farbe aendert sich).
- Gespeichert wird automatisch in `forge_save.dat` im Ordner `forge`.

## Falls die Tasten nicht passen

Unten rechts im Spiel steht, welche Taste/Achse du zuletzt gedrueckt hast (z. B. `BTN 3`).
Die Nummern kannst du in `forge/controls.cfg` anpassen. Moegliche Eintraege:
`A`, `B`, `SELECT`, `START`, `UP`, `DOWN`, `LEFT`, `RIGHT` (Steuerkreuz als Tasten, falls es keine Hat ist),
`AXIS_LX`, `AXIS_LY`, `AXIS_RX`, `AXIS_RY` (Sticks).

## Falls es nicht startet

Im Ordner `forge` liegt nach dem Start eine `log.txt`. Schick mir deren Inhalt, dann kann ich den Fehler beheben.

## Am PC testen (optional, Linux)

`g++ -O2 -std=c++14 -o forge src/main.cpp -lSDL2 -lGLESv2 -lm && ./forge --window`
(Tastatur: WASD laufen, Pfeile Kamera, E = A, Leertaste = B.)
