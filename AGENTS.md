# AGENTS.md

## Language policy
- Rede mich mit "Sie" oder "Dr. Raus" an.

- All pull request titles, pull request descriptions, commit messages, review comments, and planning text must be written in German (de-DE).

- Never write Japanese.

- Use English only for:
	- code
	- identifiers
	- existing upstream text that must be quoted verbatim

- Werden Rückfragen an den User gestellt, so ist neben der Erklärung auch eine Empfehlung für die beste Option zu präsentieren. Der User muss die Möglichkeit zur informierten Entscheidung erhalten.

- Code style: Semantisch korrekte Bezeichner ohne leading und ohne trailing underscore. Für Methoden und Membder und Klassen. Werden neue Sourcefiles benötigt ist dafür eine Freigabe einzuholen.

- Keine multiplen gleiche Stringliterale. Diese sind zu const zusammenzufassen.

- Code style: Maximum Human Readability.

- Strategie und Coding: Dies ist ein agil geführtes Innovationsprojekt. Neue Funktionen werden zuerst in Setup Dialogen implementiert, notwendige Setup Values und Keys werden ausschliesslich im zentralen key/value Hash gehalten und von dort in Richtung settings.mrmac serialisiert. Dezentrale Speicher dieser Art sind verboten. Keine überflüssigen File I/O. Es wird nur serialisiert wenn dies notwendig ist und es wird nicht reloaded aus dem Filesystem. Der k/v Hash ist stets inhaltlich gleich zum Inhalt von settings.mrmac.

- Code style: Performance maximieren.

- Code style: Keine if Ketten statt dessen tabellengesteuerte Entscheidungen.

- Code style: Tvision Upstream Treue: Keine Implementierungen um Tvision herum. Konzepte von Tvision nutzen.

- Code style: C++20 Konstrukte konsequent nutzen.

- Code style: Maximale Kaspselung bei C++ Konsstrukten.

- Code style: Kein AI Boilerplate.


