# Multi-Edit Revisited (mr)

![mr logo](documentation/pngsjpegs/mr.png)


> - "I live again." Caleb (Blood)
> - "It is never enough." Frank Cotton (Hellraiser)
> - "C makes it easy to shoot yourself in the foot; C++ makes it harder, but when you do it blows your whole leg off.“ Bjarne Stroustrup
> - "Talk is cheap. Show me the code.“ Linus Torvalds
> - "My main conclusion after spending ten years of my life working on the TeX project is that software is hard. It's harder than anything else I've ever had to do.“ Donald Knuth
> - "Coding makes me horny." Michael 'iDoc' Raus
> - "Software and cathedrals are much the same – first we build them, then we pray." Sam Redwine
> - "There are only two hard things in Computer Science: cache invalidation and naming things." Phil Karlton
> - "Theory is when you know everything but nothing works. Practice is when everything works but no one knows why. In programming, theory and practice are combined: nothing works and no one knows why." Anonymous
> - "Everyone knows that debugging is twice as hard as writing a program in the first place. So if you're as clever as you can be when you write it, how will you ever debug it?" Brian W. Kernighan
> - "Software is getting slower more rapidly than hardware becomes faster." Niklaus Wirth
> - "Code never lies; comments sometimes do." Ron Jeffries
> - "Software is a gas. It expands to fit the container it's in." Nathan Myhrvold
> - "The three chief virtues of a programmer are: Laziness, Impatience and Hubris." Larry Wall

## Look at the size of that thing!

- American Cybernetics (makers of Multi-Edit) went out of business in 2020 and stopped development of the TUI version of Multi-Edit years bevor
- mr is a rewrite of the classic programmers editor Multi-Edit by American Cybernetics for Linux terminals
- mr is constructed aroud a macro language processor, that compiles macro files based on the MEMAC script language. Now called MRMAC the language is backwards code compatible towards the MEMAC dialect but renewed and extended for modern systems. Compilation happens in-RAM through the custom mrmac compiler and supports both precompiled and on-demand compiled macro files for maximum speed. See screenshot: mr is running a utility calculator written in MRMAC
- mr uses
  - the Turbo Visison C++ rewrite TVISION from magiblot on GitHub. TVISION can also be steered from mrmac macros - just like it was in the old days with Multi-Edit
  - advanced data processing models like piecetables, addbuffers, tries and more to edit files larger than system memory and provide file I/O with blazing speed: It loads 1 GB text und under one single second und indexes the whole text in under 800 milliseconds (no BS)
  - a build in coprocessor for handling mrmac bytecode macros that can manipulate text in parallel to the user in the same window (no BS). The coprocessor supports running multiple macro jobs in parallel in different windows or multiple macrojobs in one window or both at the same time. Thecoprocessor uses multiple computes lanes in parallel: I/O, COMPUTE, MACRO and MINIMAP
  - ncursesw and is UTF8 capable
- mr supports
  - automated syntax highlighting, code folding and smart indenting for all known programming languages (except the marsian X!/&%/:-P language)
  - a macro manager for recording macros and binding them to hotkeys. You can also create, manage, edit and bind .mrmac files from inside the manager
  - virtual desktops
  - saving/reloading workspaces
  - a window manager that can tile, minimize and cascade windows
  - recursive multi file search & search and replace
  - full Perl regex PCRE2
  - a sub character minimap display
  - inter window copy & paste and copy & paste with the OS
  - stream block, line blocks and colum blocks including sorting of the marked block
  - an key mapping manager and loadable key mapping to emulate other stateless editors like Emacs, Nano or Wordstar
  - profiles per file extension or group of file extensions: You can setup the handling of code files depending of the code you edit
  - color theme loading and saving from file extension profiles
  - automatic language detection
  - automatic multi-level graphical code folding
  - smart indenting and undenting
  - printing via PDF exports
  - acquire files from the output of shell commands or pipes

## Why? Who need this?

- noone needs this - there are many good TUI editors out there
- everybody needs this - because none of the console editors around are this elegant and feature rich. Mr tries to be around in every situation you're in need of a TUI editor to manage your system, files and codebases
- why you ask? I always wanted my Multi-Edit back - but only the TUI version inspired me back in the days. I had ideas for this project for years and after i retired in 2024 i found time to export all of these ideas out of my brain and into code. Also I wanted to test what all the fuzz is about with AI and coding assistants - except I was an am not interested in vibe coding. I wanted assistance for the tedious parts. Tried several coding assistants and found Codex to best meet my needs and style of interacting - and that's simply the reason why

![mr running under vscode terminal](documentation/pngsjpegs/screenshot01.png)
![mr running in terminal showing menu structure](documentation/pngsjpegs/screenshot02.png)
![mr showing color setup dialog](documentation/pngsjpegs/screenshot03.png)
![mr showing code folding, syntax highlighting and minimap](documentation/pngsjpegs/screenshot04.png)
![mr showing key mapping](documentation/pngsjpegs/screenshot05.png)
![mr showing ui settings](documentation/pngsjpegs/screenshot06.png)
![mr showing MFS](documentation/pngsjpegs/screenshot07.png)
![mr showing macro manager](documentation/pngsjpegs/screenshot08.png)
![mr showing utility mrmac calculator source and running](documentation/pngsjpegs/screenshot09.png)
![mr showing file extension dialog language support](documentation/pngsjpegs/screenshot10.png)
![mr showing file acquisition dialog](documentation/pngsjpegs/screenshot11.png)
![mr showing window tiling](documentation/pngsjpegs/screenshot12.png)
![mr showing code folding on a macro file](documentation/pngsjpegs/screenshot13.png)
