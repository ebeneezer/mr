MR(1)                       User Commands                       MR(1)

NAME
    mr - terminal-based programmer's editor

SYNOPSIS
    mr [OPTION]... [FILE | GLOB]...

DESCRIPTION
    MR starts the interactive editor and optionally loads files, expands
    glob patterns, or runs MRMAC files after startup.

FILE ARGUMENTS
    FILE
        Load a readable file. Multiple file arguments are accepted.

    GLOB
        Load files matching a glob pattern such as *.txt or src/*.cpp.
        Quote the pattern when MR, rather than the invoking shell, should
        expand it. Without --load-recursive, matching is not recursive.

OPTIONS
    -lr, --load-recursive
        Enable recursive loading for all provided file and glob arguments.
        A directory argument loads all readable files below that directory.
        A glob argument matches recursively below its non-wildcard root.

    -rm FILE, --run-macro FILE
        Run FILE as an MRMAC file after settings, AUTOEXEC macros, optional
        workspace restoration, and command-line file loading. The option may
        be specified multiple times; macros run in argument order.

    --run-macro=FILE
        Assignment form of --run-macro FILE.

    --exit-after-run-macro
        Exit after at least one startup macro has been processed.

    -h, --help
        Print this help text and exit.

STARTUP ORDER
    MR loads settings, runs configured AUTOEXEC macros, optionally restores
    the configured workspace, loads command-line files, and finally
    processes command-line startup macros.

EXAMPLES
    Load two files:
        mr README.md src/main.cpp

    Recursively load C++ source files:
        mr -lr 'src/*.cpp'

    Load a file, run a startup macro, and exit:
        mr README.md -rm macros/session-start.mrmac \
            --exit-after-run-macro

MR(1)                                                            MR(1)
