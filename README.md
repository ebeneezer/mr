# Multi-Edit Revisited (mr) 

- American Cybernetics (makers of Multi-Edit) went out of business in 2020 and stopped development of the TUI version of Multi-Edit years bevor.
- mr is a rewrite of the classic programmer's editor Multi-Edit by American Cybernetics for Linux terminals
- mr is constructed aroud a macro language processor, that compiles macro files based on the MEMAC script language. Now called MRMAC the language is backwards code compatible the MEMAC but renewed for modern systems. The mrmac lexer and parser were contructed by using lexx and bison as the goldstandard tool under UNIX.
- mr has a centralized macro proecessor, that compiles the parsers output into bytecode streams and interpretes those to run macros. This is lightning fast and there is no I/O except reading the macro source. All is handled in-RAM for maximum speed.
- mr uses the Turbo Visison C++ rewrite TVISION from magiblot on GitHub. TVISION can also be controlled from inside mrmac macros. Just like it was in the old days with Multi-Edit.
- mr uses advanced data processing methods like piecetables to edit files larger than than the systems storage. It loads gigabytes of text in a fraction of a second.
- mr includes supper for syntax highlighting (automated) for all known programming languages. Further releases will incorporate TreeSitter for maximum Speed even in GB sized files (no joking).
- mr uses ncursesw and is UTF8 capable.

- more Multi-Edit features:
	- advanced block management
	- keystroke macros
	- and much much more coming up...

- I own a Multi-Edit license and all written documentation: Users manual and MEMAC (MRMAC) reference is also part of this repo. Please take a look!


> [!NOTE]
> I am looking for C/C++ devs knowing Multi-Edit and his fantastic features! If you are into retro coding and want an editor you can call home - lets volunteer and create a terminal gem with me!

![mr running under vscode terminal](screenshots/screenshot01.png)
![mr running in terminal showing menu structure](screenshots/screenshot02.png)
