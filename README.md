![mr logo](ui/mr.png)

> [!NOTE]
> - "I live again." Caleb (Blood)
> - "It is never enough." Frank Cotton (Hellraiser)
> - "C makes it easy to shoot yourself in the foot; C++ makes it harder, but when you do it blows your whole leg off.“ Bjarne Stroustrup
> - „Talk is cheap. Show me the code.“ Linus Torvalds
> - „My main conclusion after spending ten years of my life working on the TeX project is that software is hard. It's harder than anything else I've ever had to do.“ Donald Knuth
> - "Coding makes me horny." Michael 'iDoc' Raus
> - "Software and cathedrals are much the same – first we build them, then we pray." Sam Redwine
> - "There are only two hard things in Computer Science: cache invalidation and naming things." Phil Karlton
> - "Theory is when you know everything but nothing works. Practice is when everything works but no one knows why. In programming, theory and practice are combined: nothing works and no one knows why." Anonymous

# Multi-Edit Revisited (mr) 

- American Cybernetics (makers of Multi-Edit) went out of business in 2020 and stopped development of the TUI version of Multi-Edit years bevor.
- mr is a rewrite of the classic programmer's editor Multi-Edit by American Cybernetics for Linux terminals
- mr is constructed aroud a macro language processor, that compiles macro files based on the MEMAC script language. Now called MRMAC the language is backwards code compatible the MEMAC but renewed for modern systems. The mrmac lexer and parser were contructed by using lexx and bison as the goldstandard tools under UNIX. All is handled in-RAM for maximum speed.
- mr uses the Turbo Visison C++ rewrite TVISION from magiblot on GitHub. TVISION can also be steered from mrmac macros - just like it was in the old days with Multi-Edit.
- mr uses advanced data processing methods like piecetables to edit files larger than system memory.
- mr uses advanced techniques for loading and saving huge files: It loads 1 GB text und under one single second und indexes the whole text in under 500 milliseconds (no BS)
- mr uses a build in coprocessor for handling bytecode compiled mrmac macrofiles that can manipulate text in parallel to the user in the same window (no BS). The coprocessor supports running multiple macro jobs in parallel in different windows or multiple macrojobs in one window.
- mr includes support for syntax highlighting (automated) for all known programming languages. Further releases will incorporate TreeSitter for maximum speed.
- mr uses ncursesw and is UTF8 capable.
- mr has a build in macro manager for recording macros and binding them to hotkeys. You can create, manage, edit and bind .mrmac files from inside the manager (no separate keystroke editor).

## MRMAC v1 verification

- run: `make mrmac-v1-check`
- status and scope: `documentation/MRMAC_V1_STATUS.md`

- I own a Multi-Edit license and all written documentation: Users manual and MEMAC (MRMAC) reference is also part of this repo. Please take a look!


> [!NOTE]
> I am looking for C/C++ devs knowing Multi-Edit and his fantastic features! If coding makes you horny you or you are looking for an editor you can call home - volunteer and create a terminal gem with me!

![mr running under vscode terminal](screenshots/screenshot01.png)
![mr running in terminal showing menu structure](screenshots/screenshot02.png)
