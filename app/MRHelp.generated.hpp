#ifndef MR_HELP_GENERATED_HPP
#define MR_HELP_GENERATED_HPP

static const char kMrEmbeddedHelpMarkdown[] = R"MRHELP(
# MR Command Line Help

## File Arguments

Pass one or more file paths on the command line to load them on startup.

You can also pass glob patterns (for example `*.txt` or `src/*.cpp`).

## `--load-recursive`, `-lr`

Enable recursive startup loading for the provided path arguments.

- If a path argument contains wildcards, MR scans recursively below the argument path root and loads all matching files.
- If a path argument has no wildcards:
  - a file path loads that single file
  - a directory path loads all readable files recursively below that directory

## `--help`, `-h`

Print this help text and exit.
)MRHELP";

#endif
