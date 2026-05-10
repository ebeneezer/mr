#!/usr/bin/env fish
# fish_lexer_training_corpus_1000_lines.fish
# Synthetic Fish lexer-training corpus; not intended to be executed.
set -g script_name (status filename)
set -g version 0.1.0
set -g plain_var plain
set -gx EXPORTED_FISH_VALUE exported
set -g global_list one two three
set -g path_value /tmp/archive.tar.gz
string upper -- $plain_var >/dev/null
string lower -- $plain_var >/dev/null
string escape -- $plain_var >/dev/null
path basename -- $path_value >/dev/null
function log_message --description 'log a message'
    set -l level INFO
    if test (count $argv) -gt 0; set level $argv[1]; set -e argv[1]; end
    printf '[%s] %s\n' $level "$argv" >&2
end
function arithmetic_examples
    set -l x 10; set x (math "$x + 1"); set x (math "$x * 2"); set x (math "floor($x / 3)"); set x (math "$x % 5"); echo $x >/dev/null
end
function conditional_examples
    set -l value alpha; if test (count $argv) -gt 0; set value $argv[1]; end
    if string match -qr '^[[:alpha:]_][[:alnum:]_]*$' -- $value; echo id; else if contains -- $value yes no maybe; echo choice; else; string escape -- $value; end >/dev/null
    switch $value; case alpha beta; echo ab; case '[0-9]*'; echo num; case --help -h; echo help; case '*'; echo default; end >/dev/null
end
function loop_examples
    for item in $global_list; test -z "$item"; and continue; echo $item >/dev/null; end
    set -l i 0; while test $i -lt 3; set i (math "$i + 1"); end
end
function read_examples
    printf '%s\n' one 'two words' three | while read -l line; printf 'line=%s\n' $line >/dev/null; end
end
function redirection_examples
    echo stdout >/tmp/fish_lexer_stdout.txt; echo stderr 2>/tmp/fish_lexer_stderr.txt; begin; echo grouped; echo output; end >/dev/null
end
function command_substitution_examples
    set -l now (date +%s); set -l words (printf '%s\n' alpha beta gamma); set -l joined (string join , -- $words); echo $now $joined >/dev/null
end
function argparse_examples
    argparse h/help v/verbose o/output= -- $argv; or return
    set -q _flag_help; and echo help >/dev/null
end
function variable_scope_examples
    set -l local_value local; set -g global_value global; set -U universal_value universal; set -q local_value; and echo exists >/dev/null; set -e local_value
end
function completion_examples
    complete -c lexer-training -s h -l help -d 'show help'; complete -c lexer-training -s o -l output -r -d 'output file'; abbr -a lxtrain 'lexer-training'
end
function string_examples
    set -l text 'alpha beta gamma'; string match -r '[a-z]+' -- $text >/dev/null; string replace -a beta BETA -- $text >/dev/null; string split ' ' -- $text >/dev/null
end
# generated Fish block 001: lists, switch, command substitution
function generated_fish_001
    set -l n 1
    set -l label block-001
    set -l values $label $plain_var 1
    for value in $values
        if string match -qr '^block-[0-9]+' -- $value
            set n (math "$n + "(string length -- $value))
        end
        switch $value
            case 'block-*'
                string escape -- $value >/dev/null
            case '[0-9]*'
                set n (math "$n + $value")
            case '*'
                string upper -- $value >/dev/null
        end
    end
    printf '%s\n' 'literal $not_expanded and (not_executed)' >/dev/null
    return (math "$n % 255")
end
# generated Fish block 002: lists, switch, command substitution
function generated_fish_002
    set -l n 2
    set -l label block-002
    set -l values $label $plain_var 2
    for value in $values
        if string match -qr '^block-[0-9]+' -- $value
            set n (math "$n + "(string length -- $value))
        end
        switch $value
            case 'block-*'
                string escape -- $value >/dev/null
            case '[0-9]*'
                set n (math "$n + $value")
            case '*'
                string upper -- $value >/dev/null
        end
    end
    printf '%s\n' 'literal $not_expanded and (not_executed)' >/dev/null
    return (math "$n % 255")
end
# generated Fish block 003: lists, switch, command substitution
function generated_fish_003
    set -l n 3
    set -l label block-003
    set -l values $label $plain_var 3
    for value in $values
        if string match -qr '^block-[0-9]+' -- $value
            set n (math "$n + "(string length -- $value))
        end
        switch $value
            case 'block-*'
                string escape -- $value >/dev/null
            case '[0-9]*'
                set n (math "$n + $value")
            case '*'
                string upper -- $value >/dev/null
        end
    end
    printf '%s\n' 'literal $not_expanded and (not_executed)' >/dev/null
    return (math "$n % 255")
end
# generated Fish block 004: lists, switch, command substitution
function generated_fish_004
    set -l n 4
    set -l label block-004
    set -l values $label $plain_var 4
    for value in $values
        if string match -qr '^block-[0-9]+' -- $value
            set n (math "$n + "(string length -- $value))
        end
        switch $value
            case 'block-*'
                string escape -- $value >/dev/null
            case '[0-9]*'
                set n (math "$n + $value")
            case '*'
                string upper -- $value >/dev/null
        end
    end
    printf '%s\n' 'literal $not_expanded and (not_executed)' >/dev/null
    return (math "$n % 255")
end
# generated Fish block 005: lists, switch, command substitution
function generated_fish_005
    set -l n 5
    set -l label block-005
    set -l values $label $plain_var 5
    for value in $values
        if string match -qr '^block-[0-9]+' -- $value
            set n (math "$n + "(string length -- $value))
        end
        switch $value
            case 'block-*'
                string escape -- $value >/dev/null
            case '[0-9]*'
                set n (math "$n + $value")
            case '*'
                string upper -- $value >/dev/null
        end
    end
    printf '%s\n' 'literal $not_expanded and (not_executed)' >/dev/null
    return (math "$n % 255")
end
# generated Fish block 006: lists, switch, command substitution
function generated_fish_006
    set -l n 6
    set -l label block-006
    set -l values $label $plain_var 6
    for value in $values
        if string match -qr '^block-[0-9]+' -- $value
            set n (math "$n + "(string length -- $value))
        end
        switch $value
            case 'block-*'
                string escape -- $value >/dev/null
            case '[0-9]*'
                set n (math "$n + $value")
            case '*'
                string upper -- $value >/dev/null
        end
    end
    printf '%s\n' 'literal $not_expanded and (not_executed)' >/dev/null
    return (math "$n % 255")
end
# generated Fish block 007: lists, switch, command substitution
function generated_fish_007
    set -l n 7
    set -l label block-007
    set -l values $label $plain_var 7
    for value in $values
        if string match -qr '^block-[0-9]+' -- $value
            set n (math "$n + "(string length -- $value))
        end
        switch $value
            case 'block-*'
                string escape -- $value >/dev/null
            case '[0-9]*'
                set n (math "$n + $value")
            case '*'
                string upper -- $value >/dev/null
        end
    end
    printf '%s\n' 'literal $not_expanded and (not_executed)' >/dev/null
    return (math "$n % 255")
end
# generated Fish block 008: lists, switch, command substitution
function generated_fish_008
    set -l n 8
    set -l label block-008
    set -l values $label $plain_var 8
    for value in $values
        if string match -qr '^block-[0-9]+' -- $value
            set n (math "$n + "(string length -- $value))
        end
        switch $value
            case 'block-*'
                string escape -- $value >/dev/null
            case '[0-9]*'
                set n (math "$n + $value")
            case '*'
                string upper -- $value >/dev/null
        end
    end
    printf '%s\n' 'literal $not_expanded and (not_executed)' >/dev/null
    return (math "$n % 255")
end
# generated Fish block 009: lists, switch, command substitution
function generated_fish_009
    set -l n 9
    set -l label block-009
    set -l values $label $plain_var 9
    for value in $values
        if string match -qr '^block-[0-9]+' -- $value
            set n (math "$n + "(string length -- $value))
        end
        switch $value
            case 'block-*'
                string escape -- $value >/dev/null
            case '[0-9]*'
                set n (math "$n + $value")
            case '*'
                string upper -- $value >/dev/null
        end
    end
    printf '%s\n' 'literal $not_expanded and (not_executed)' >/dev/null
    return (math "$n % 255")
end
# generated Fish block 010: lists, switch, command substitution
function generated_fish_010
    set -l n 10
    set -l label block-010
    set -l values $label $plain_var 10
    for value in $values
        if string match -qr '^block-[0-9]+' -- $value
            set n (math "$n + "(string length -- $value))
        end
        switch $value
            case 'block-*'
                string escape -- $value >/dev/null
            case '[0-9]*'
                set n (math "$n + $value")
            case '*'
                string upper -- $value >/dev/null
        end
    end
    printf '%s\n' 'literal $not_expanded and (not_executed)' >/dev/null
    return (math "$n % 255")
end
# generated Fish block 011: lists, switch, command substitution
function generated_fish_011
    set -l n 11
    set -l label block-011
    set -l values $label $plain_var 11
    for value in $values
        if string match -qr '^block-[0-9]+' -- $value
            set n (math "$n + "(string length -- $value))
        end
        switch $value
            case 'block-*'
                string escape -- $value >/dev/null
            case '[0-9]*'
                set n (math "$n + $value")
            case '*'
                string upper -- $value >/dev/null
        end
    end
    printf '%s\n' 'literal $not_expanded and (not_executed)' >/dev/null
    return (math "$n % 255")
end
# generated Fish block 012: lists, switch, command substitution
function generated_fish_012
    set -l n 12
    set -l label block-012
    set -l values $label $plain_var 12
    for value in $values
        if string match -qr '^block-[0-9]+' -- $value
            set n (math "$n + "(string length -- $value))
        end
        switch $value
            case 'block-*'
                string escape -- $value >/dev/null
            case '[0-9]*'
                set n (math "$n + $value")
            case '*'
                string upper -- $value >/dev/null
        end
    end
    printf '%s\n' 'literal $not_expanded and (not_executed)' >/dev/null
    return (math "$n % 255")
end
# generated Fish block 013: lists, switch, command substitution
function generated_fish_013
    set -l n 13
    set -l label block-013
    set -l values $label $plain_var 13
    for value in $values
        if string match -qr '^block-[0-9]+' -- $value
            set n (math "$n + "(string length -- $value))
        end
        switch $value
            case 'block-*'
                string escape -- $value >/dev/null
            case '[0-9]*'
                set n (math "$n + $value")
            case '*'
                string upper -- $value >/dev/null
        end
    end
    printf '%s\n' 'literal $not_expanded and (not_executed)' >/dev/null
    return (math "$n % 255")
end
# generated Fish block 014: lists, switch, command substitution
function generated_fish_014
    set -l n 14
    set -l label block-014
    set -l values $label $plain_var 14
    for value in $values
        if string match -qr '^block-[0-9]+' -- $value
            set n (math "$n + "(string length -- $value))
        end
        switch $value
            case 'block-*'
                string escape -- $value >/dev/null
            case '[0-9]*'
                set n (math "$n + $value")
            case '*'
                string upper -- $value >/dev/null
        end
    end
    printf '%s\n' 'literal $not_expanded and (not_executed)' >/dev/null
    return (math "$n % 255")
end
# generated Fish block 015: lists, switch, command substitution
function generated_fish_015
    set -l n 15
    set -l label block-015
    set -l values $label $plain_var 15
    for value in $values
        if string match -qr '^block-[0-9]+' -- $value
            set n (math "$n + "(string length -- $value))
        end
        switch $value
            case 'block-*'
                string escape -- $value >/dev/null
            case '[0-9]*'
                set n (math "$n + $value")
            case '*'
                string upper -- $value >/dev/null
        end
    end
    printf '%s\n' 'literal $not_expanded and (not_executed)' >/dev/null
    return (math "$n % 255")
end
# generated Fish block 016: lists, switch, command substitution
function generated_fish_016
    set -l n 16
    set -l label block-016
    set -l values $label $plain_var 16
    for value in $values
        if string match -qr '^block-[0-9]+' -- $value
            set n (math "$n + "(string length -- $value))
        end
        switch $value
            case 'block-*'
                string escape -- $value >/dev/null
            case '[0-9]*'
                set n (math "$n + $value")
            case '*'
                string upper -- $value >/dev/null
        end
    end
    printf '%s\n' 'literal $not_expanded and (not_executed)' >/dev/null
    return (math "$n % 255")
end
# generated Fish block 017: lists, switch, command substitution
function generated_fish_017
    set -l n 17
    set -l label block-017
    set -l values $label $plain_var 17
    for value in $values
        if string match -qr '^block-[0-9]+' -- $value
            set n (math "$n + "(string length -- $value))
        end
        switch $value
            case 'block-*'
                string escape -- $value >/dev/null
            case '[0-9]*'
                set n (math "$n + $value")
            case '*'
                string upper -- $value >/dev/null
        end
    end
    printf '%s\n' 'literal $not_expanded and (not_executed)' >/dev/null
    return (math "$n % 255")
end
# generated Fish block 018: lists, switch, command substitution
function generated_fish_018
    set -l n 18
    set -l label block-018
    set -l values $label $plain_var 18
    for value in $values
        if string match -qr '^block-[0-9]+' -- $value
            set n (math "$n + "(string length -- $value))
        end
        switch $value
            case 'block-*'
                string escape -- $value >/dev/null
            case '[0-9]*'
                set n (math "$n + $value")
            case '*'
                string upper -- $value >/dev/null
        end
    end
    printf '%s\n' 'literal $not_expanded and (not_executed)' >/dev/null
    return (math "$n % 255")
end
# generated Fish block 019: lists, switch, command substitution
function generated_fish_019
    set -l n 19
    set -l label block-019
    set -l values $label $plain_var 19
    for value in $values
        if string match -qr '^block-[0-9]+' -- $value
            set n (math "$n + "(string length -- $value))
        end
        switch $value
            case 'block-*'
                string escape -- $value >/dev/null
            case '[0-9]*'
                set n (math "$n + $value")
            case '*'
                string upper -- $value >/dev/null
        end
    end
    printf '%s\n' 'literal $not_expanded and (not_executed)' >/dev/null
    return (math "$n % 255")
end
# generated Fish block 020: lists, switch, command substitution
function generated_fish_020
    set -l n 20
    set -l label block-020
    set -l values $label $plain_var 20
    for value in $values
        if string match -qr '^block-[0-9]+' -- $value
            set n (math "$n + "(string length -- $value))
        end
        switch $value
            case 'block-*'
                string escape -- $value >/dev/null
            case '[0-9]*'
                set n (math "$n + $value")
            case '*'
                string upper -- $value >/dev/null
        end
    end
    printf '%s\n' 'literal $not_expanded and (not_executed)' >/dev/null
    return (math "$n % 255")
end
# generated Fish block 021: lists, switch, command substitution
function generated_fish_021
    set -l n 21
    set -l label block-021
    set -l values $label $plain_var 21
    for value in $values
        if string match -qr '^block-[0-9]+' -- $value
            set n (math "$n + "(string length -- $value))
        end
        switch $value
            case 'block-*'
                string escape -- $value >/dev/null
            case '[0-9]*'
                set n (math "$n + $value")
            case '*'
                string upper -- $value >/dev/null
        end
    end
    printf '%s\n' 'literal $not_expanded and (not_executed)' >/dev/null
    return (math "$n % 255")
end
# generated Fish block 022: lists, switch, command substitution
function generated_fish_022
    set -l n 22
    set -l label block-022
    set -l values $label $plain_var 22
    for value in $values
        if string match -qr '^block-[0-9]+' -- $value
            set n (math "$n + "(string length -- $value))
        end
        switch $value
            case 'block-*'
                string escape -- $value >/dev/null
            case '[0-9]*'
                set n (math "$n + $value")
            case '*'
                string upper -- $value >/dev/null
        end
    end
    printf '%s\n' 'literal $not_expanded and (not_executed)' >/dev/null
    return (math "$n % 255")
end
# generated Fish block 023: lists, switch, command substitution
function generated_fish_023
    set -l n 23
    set -l label block-023
    set -l values $label $plain_var 23
    for value in $values
        if string match -qr '^block-[0-9]+' -- $value
            set n (math "$n + "(string length -- $value))
        end
        switch $value
            case 'block-*'
                string escape -- $value >/dev/null
            case '[0-9]*'
                set n (math "$n + $value")
            case '*'
                string upper -- $value >/dev/null
        end
    end
    printf '%s\n' 'literal $not_expanded and (not_executed)' >/dev/null
    return (math "$n % 255")
end
# generated Fish block 024: lists, switch, command substitution
function generated_fish_024
    set -l n 24
    set -l label block-024
    set -l values $label $plain_var 24
    for value in $values
        if string match -qr '^block-[0-9]+' -- $value
            set n (math "$n + "(string length -- $value))
        end
        switch $value
            case 'block-*'
                string escape -- $value >/dev/null
            case '[0-9]*'
                set n (math "$n + $value")
            case '*'
                string upper -- $value >/dev/null
        end
    end
    printf '%s\n' 'literal $not_expanded and (not_executed)' >/dev/null
    return (math "$n % 255")
end
# generated Fish block 025: lists, switch, command substitution
function generated_fish_025
    set -l n 25
    set -l label block-025
    set -l values $label $plain_var 25
    for value in $values
        if string match -qr '^block-[0-9]+' -- $value
            set n (math "$n + "(string length -- $value))
        end
        switch $value
            case 'block-*'
                string escape -- $value >/dev/null
            case '[0-9]*'
                set n (math "$n + $value")
            case '*'
                string upper -- $value >/dev/null
        end
    end
    printf '%s\n' 'literal $not_expanded and (not_executed)' >/dev/null
    return (math "$n % 255")
end
# generated Fish block 026: lists, switch, command substitution
function generated_fish_026
    set -l n 26
    set -l label block-026
    set -l values $label $plain_var 26
    for value in $values
        if string match -qr '^block-[0-9]+' -- $value
            set n (math "$n + "(string length -- $value))
        end
        switch $value
            case 'block-*'
                string escape -- $value >/dev/null
            case '[0-9]*'
                set n (math "$n + $value")
            case '*'
                string upper -- $value >/dev/null
        end
    end
    printf '%s\n' 'literal $not_expanded and (not_executed)' >/dev/null
    return (math "$n % 255")
end
# generated Fish block 027: lists, switch, command substitution
function generated_fish_027
    set -l n 27
    set -l label block-027
    set -l values $label $plain_var 27
    for value in $values
        if string match -qr '^block-[0-9]+' -- $value
            set n (math "$n + "(string length -- $value))
        end
        switch $value
            case 'block-*'
                string escape -- $value >/dev/null
            case '[0-9]*'
                set n (math "$n + $value")
            case '*'
                string upper -- $value >/dev/null
        end
    end
    printf '%s\n' 'literal $not_expanded and (not_executed)' >/dev/null
    return (math "$n % 255")
end
# generated Fish block 028: lists, switch, command substitution
function generated_fish_028
    set -l n 28
    set -l label block-028
    set -l values $label $plain_var 28
    for value in $values
        if string match -qr '^block-[0-9]+' -- $value
            set n (math "$n + "(string length -- $value))
        end
        switch $value
            case 'block-*'
                string escape -- $value >/dev/null
            case '[0-9]*'
                set n (math "$n + $value")
            case '*'
                string upper -- $value >/dev/null
        end
    end
    printf '%s\n' 'literal $not_expanded and (not_executed)' >/dev/null
    return (math "$n % 255")
end
# generated Fish block 029: lists, switch, command substitution
function generated_fish_029
    set -l n 29
    set -l label block-029
    set -l values $label $plain_var 29
    for value in $values
        if string match -qr '^block-[0-9]+' -- $value
            set n (math "$n + "(string length -- $value))
        end
        switch $value
            case 'block-*'
                string escape -- $value >/dev/null
            case '[0-9]*'
                set n (math "$n + $value")
            case '*'
                string upper -- $value >/dev/null
        end
    end
    printf '%s\n' 'literal $not_expanded and (not_executed)' >/dev/null
    return (math "$n % 255")
end
# generated Fish block 030: lists, switch, command substitution
function generated_fish_030
    set -l n 30
    set -l label block-030
    set -l values $label $plain_var 30
    for value in $values
        if string match -qr '^block-[0-9]+' -- $value
            set n (math "$n + "(string length -- $value))
        end
        switch $value
            case 'block-*'
                string escape -- $value >/dev/null
            case '[0-9]*'
                set n (math "$n + $value")
            case '*'
                string upper -- $value >/dev/null
        end
    end
    printf '%s\n' 'literal $not_expanded and (not_executed)' >/dev/null
    return (math "$n % 255")
end
# generated Fish block 031: lists, switch, command substitution
function generated_fish_031
    set -l n 31
    set -l label block-031
    set -l values $label $plain_var 31
    for value in $values
        if string match -qr '^block-[0-9]+' -- $value
            set n (math "$n + "(string length -- $value))
        end
        switch $value
            case 'block-*'
                string escape -- $value >/dev/null
            case '[0-9]*'
                set n (math "$n + $value")
            case '*'
                string upper -- $value >/dev/null
        end
    end
    printf '%s\n' 'literal $not_expanded and (not_executed)' >/dev/null
    return (math "$n % 255")
end
# generated Fish block 032: lists, switch, command substitution
function generated_fish_032
    set -l n 32
    set -l label block-032
    set -l values $label $plain_var 32
    for value in $values
        if string match -qr '^block-[0-9]+' -- $value
            set n (math "$n + "(string length -- $value))
        end
        switch $value
            case 'block-*'
                string escape -- $value >/dev/null
            case '[0-9]*'
                set n (math "$n + $value")
            case '*'
                string upper -- $value >/dev/null
        end
    end
    printf '%s\n' 'literal $not_expanded and (not_executed)' >/dev/null
    return (math "$n % 255")
end
# generated Fish block 033: lists, switch, command substitution
function generated_fish_033
    set -l n 33
    set -l label block-033
    set -l values $label $plain_var 33
    for value in $values
        if string match -qr '^block-[0-9]+' -- $value
            set n (math "$n + "(string length -- $value))
        end
        switch $value
            case 'block-*'
                string escape -- $value >/dev/null
            case '[0-9]*'
                set n (math "$n + $value")
            case '*'
                string upper -- $value >/dev/null
        end
    end
    printf '%s\n' 'literal $not_expanded and (not_executed)' >/dev/null
    return (math "$n % 255")
end
# generated Fish block 034: lists, switch, command substitution
function generated_fish_034
    set -l n 34
    set -l label block-034
    set -l values $label $plain_var 34
    for value in $values
        if string match -qr '^block-[0-9]+' -- $value
            set n (math "$n + "(string length -- $value))
        end
        switch $value
            case 'block-*'
                string escape -- $value >/dev/null
            case '[0-9]*'
                set n (math "$n + $value")
            case '*'
                string upper -- $value >/dev/null
        end
    end
    printf '%s\n' 'literal $not_expanded and (not_executed)' >/dev/null
    return (math "$n % 255")
end
# generated Fish block 035: lists, switch, command substitution
function generated_fish_035
    set -l n 35
    set -l label block-035
    set -l values $label $plain_var 35
    for value in $values
        if string match -qr '^block-[0-9]+' -- $value
            set n (math "$n + "(string length -- $value))
        end
        switch $value
            case 'block-*'
                string escape -- $value >/dev/null
            case '[0-9]*'
                set n (math "$n + $value")
            case '*'
                string upper -- $value >/dev/null
        end
    end
    printf '%s\n' 'literal $not_expanded and (not_executed)' >/dev/null
    return (math "$n % 255")
end
# generated Fish block 036: lists, switch, command substitution
function generated_fish_036
    set -l n 36
    set -l label block-036
    set -l values $label $plain_var 36
    for value in $values
        if string match -qr '^block-[0-9]+' -- $value
            set n (math "$n + "(string length -- $value))
        end
        switch $value
            case 'block-*'
                string escape -- $value >/dev/null
            case '[0-9]*'
                set n (math "$n + $value")
            case '*'
                string upper -- $value >/dev/null
        end
    end
    printf '%s\n' 'literal $not_expanded and (not_executed)' >/dev/null
    return (math "$n % 255")
end
set -l fish_filler_0001 filler-1; string match -qr '^filler-[0-9]+' -- $fish_filler_0001; and string escape -- $fish_filler_0001 >/dev/null
set -l fish_filler_0002 filler-2; string match -qr '^filler-[0-9]+' -- $fish_filler_0002; and string escape -- $fish_filler_0002 >/dev/null
set -l fish_filler_0003 filler-3; string match -qr '^filler-[0-9]+' -- $fish_filler_0003; and string escape -- $fish_filler_0003 >/dev/null
set -l fish_filler_0004 filler-4; string match -qr '^filler-[0-9]+' -- $fish_filler_0004; and string escape -- $fish_filler_0004 >/dev/null
set -l fish_filler_0005 filler-5; string match -qr '^filler-[0-9]+' -- $fish_filler_0005; and string escape -- $fish_filler_0005 >/dev/null
set -l fish_filler_0006 filler-6; string match -qr '^filler-[0-9]+' -- $fish_filler_0006; and string escape -- $fish_filler_0006 >/dev/null
set -l fish_filler_0007 filler-7; string match -qr '^filler-[0-9]+' -- $fish_filler_0007; and string escape -- $fish_filler_0007 >/dev/null
set -l fish_filler_0008 filler-8; string match -qr '^filler-[0-9]+' -- $fish_filler_0008; and string escape -- $fish_filler_0008 >/dev/null
set -l fish_filler_0009 filler-9; string match -qr '^filler-[0-9]+' -- $fish_filler_0009; and string escape -- $fish_filler_0009 >/dev/null
set -l fish_filler_0010 filler-10; string match -qr '^filler-[0-9]+' -- $fish_filler_0010; and string escape -- $fish_filler_0010 >/dev/null
set -l fish_filler_0011 filler-11; string match -qr '^filler-[0-9]+' -- $fish_filler_0011; and string escape -- $fish_filler_0011 >/dev/null
set -l fish_filler_0012 filler-12; string match -qr '^filler-[0-9]+' -- $fish_filler_0012; and string escape -- $fish_filler_0012 >/dev/null
set -l fish_filler_0013 filler-13; string match -qr '^filler-[0-9]+' -- $fish_filler_0013; and string escape -- $fish_filler_0013 >/dev/null
set -l fish_filler_0014 filler-14; string match -qr '^filler-[0-9]+' -- $fish_filler_0014; and string escape -- $fish_filler_0014 >/dev/null
set -l fish_filler_0015 filler-15; string match -qr '^filler-[0-9]+' -- $fish_filler_0015; and string escape -- $fish_filler_0015 >/dev/null
set -l fish_filler_0016 filler-16; string match -qr '^filler-[0-9]+' -- $fish_filler_0016; and string escape -- $fish_filler_0016 >/dev/null
set -l fish_filler_0017 filler-17; string match -qr '^filler-[0-9]+' -- $fish_filler_0017; and string escape -- $fish_filler_0017 >/dev/null
set -l fish_filler_0018 filler-18; string match -qr '^filler-[0-9]+' -- $fish_filler_0018; and string escape -- $fish_filler_0018 >/dev/null
set -l fish_filler_0019 filler-19; string match -qr '^filler-[0-9]+' -- $fish_filler_0019; and string escape -- $fish_filler_0019 >/dev/null
set -l fish_filler_0020 filler-20; string match -qr '^filler-[0-9]+' -- $fish_filler_0020; and string escape -- $fish_filler_0020 >/dev/null
set -l fish_filler_0021 filler-21; string match -qr '^filler-[0-9]+' -- $fish_filler_0021; and string escape -- $fish_filler_0021 >/dev/null
set -l fish_filler_0022 filler-22; string match -qr '^filler-[0-9]+' -- $fish_filler_0022; and string escape -- $fish_filler_0022 >/dev/null
set -l fish_filler_0023 filler-23; string match -qr '^filler-[0-9]+' -- $fish_filler_0023; and string escape -- $fish_filler_0023 >/dev/null
set -l fish_filler_0024 filler-24; string match -qr '^filler-[0-9]+' -- $fish_filler_0024; and string escape -- $fish_filler_0024 >/dev/null
set -l fish_filler_0025 filler-25; string match -qr '^filler-[0-9]+' -- $fish_filler_0025; and string escape -- $fish_filler_0025 >/dev/null
set -l fish_filler_0026 filler-26; string match -qr '^filler-[0-9]+' -- $fish_filler_0026; and string escape -- $fish_filler_0026 >/dev/null
set -l fish_filler_0027 filler-27; string match -qr '^filler-[0-9]+' -- $fish_filler_0027; and string escape -- $fish_filler_0027 >/dev/null
set -l fish_filler_0028 filler-28; string match -qr '^filler-[0-9]+' -- $fish_filler_0028; and string escape -- $fish_filler_0028 >/dev/null
set -l fish_filler_0029 filler-29; string match -qr '^filler-[0-9]+' -- $fish_filler_0029; and string escape -- $fish_filler_0029 >/dev/null
set -l fish_filler_0030 filler-30; string match -qr '^filler-[0-9]+' -- $fish_filler_0030; and string escape -- $fish_filler_0030 >/dev/null
set -l fish_filler_0031 filler-31; string match -qr '^filler-[0-9]+' -- $fish_filler_0031; and string escape -- $fish_filler_0031 >/dev/null
set -l fish_filler_0032 filler-32; string match -qr '^filler-[0-9]+' -- $fish_filler_0032; and string escape -- $fish_filler_0032 >/dev/null
set -l fish_filler_0033 filler-33; string match -qr '^filler-[0-9]+' -- $fish_filler_0033; and string escape -- $fish_filler_0033 >/dev/null
set -l fish_filler_0034 filler-34; string match -qr '^filler-[0-9]+' -- $fish_filler_0034; and string escape -- $fish_filler_0034 >/dev/null
set -l fish_filler_0035 filler-35; string match -qr '^filler-[0-9]+' -- $fish_filler_0035; and string escape -- $fish_filler_0035 >/dev/null
set -l fish_filler_0036 filler-36; string match -qr '^filler-[0-9]+' -- $fish_filler_0036; and string escape -- $fish_filler_0036 >/dev/null
set -l fish_filler_0037 filler-37; string match -qr '^filler-[0-9]+' -- $fish_filler_0037; and string escape -- $fish_filler_0037 >/dev/null
set -l fish_filler_0038 filler-38; string match -qr '^filler-[0-9]+' -- $fish_filler_0038; and string escape -- $fish_filler_0038 >/dev/null
set -l fish_filler_0039 filler-39; string match -qr '^filler-[0-9]+' -- $fish_filler_0039; and string escape -- $fish_filler_0039 >/dev/null
set -l fish_filler_0040 filler-40; string match -qr '^filler-[0-9]+' -- $fish_filler_0040; and string escape -- $fish_filler_0040 >/dev/null
set -l fish_filler_0041 filler-41; string match -qr '^filler-[0-9]+' -- $fish_filler_0041; and string escape -- $fish_filler_0041 >/dev/null
set -l fish_filler_0042 filler-42; string match -qr '^filler-[0-9]+' -- $fish_filler_0042; and string escape -- $fish_filler_0042 >/dev/null
set -l fish_filler_0043 filler-43; string match -qr '^filler-[0-9]+' -- $fish_filler_0043; and string escape -- $fish_filler_0043 >/dev/null
set -l fish_filler_0044 filler-44; string match -qr '^filler-[0-9]+' -- $fish_filler_0044; and string escape -- $fish_filler_0044 >/dev/null
set -l fish_filler_0045 filler-45; string match -qr '^filler-[0-9]+' -- $fish_filler_0045; and string escape -- $fish_filler_0045 >/dev/null
set -l fish_filler_0046 filler-46; string match -qr '^filler-[0-9]+' -- $fish_filler_0046; and string escape -- $fish_filler_0046 >/dev/null
set -l fish_filler_0047 filler-47; string match -qr '^filler-[0-9]+' -- $fish_filler_0047; and string escape -- $fish_filler_0047 >/dev/null
set -l fish_filler_0048 filler-48; string match -qr '^filler-[0-9]+' -- $fish_filler_0048; and string escape -- $fish_filler_0048 >/dev/null
set -l fish_filler_0049 filler-49; string match -qr '^filler-[0-9]+' -- $fish_filler_0049; and string escape -- $fish_filler_0049 >/dev/null
set -l fish_filler_0050 filler-50; string match -qr '^filler-[0-9]+' -- $fish_filler_0050; and string escape -- $fish_filler_0050 >/dev/null
set -l fish_filler_0051 filler-51; string match -qr '^filler-[0-9]+' -- $fish_filler_0051; and string escape -- $fish_filler_0051 >/dev/null
set -l fish_filler_0052 filler-52; string match -qr '^filler-[0-9]+' -- $fish_filler_0052; and string escape -- $fish_filler_0052 >/dev/null
set -l fish_filler_0053 filler-53; string match -qr '^filler-[0-9]+' -- $fish_filler_0053; and string escape -- $fish_filler_0053 >/dev/null
set -l fish_filler_0054 filler-54; string match -qr '^filler-[0-9]+' -- $fish_filler_0054; and string escape -- $fish_filler_0054 >/dev/null
set -l fish_filler_0055 filler-55; string match -qr '^filler-[0-9]+' -- $fish_filler_0055; and string escape -- $fish_filler_0055 >/dev/null
set -l fish_filler_0056 filler-56; string match -qr '^filler-[0-9]+' -- $fish_filler_0056; and string escape -- $fish_filler_0056 >/dev/null
set -l fish_filler_0057 filler-57; string match -qr '^filler-[0-9]+' -- $fish_filler_0057; and string escape -- $fish_filler_0057 >/dev/null
set -l fish_filler_0058 filler-58; string match -qr '^filler-[0-9]+' -- $fish_filler_0058; and string escape -- $fish_filler_0058 >/dev/null
set -l fish_filler_0059 filler-59; string match -qr '^filler-[0-9]+' -- $fish_filler_0059; and string escape -- $fish_filler_0059 >/dev/null
set -l fish_filler_0060 filler-60; string match -qr '^filler-[0-9]+' -- $fish_filler_0060; and string escape -- $fish_filler_0060 >/dev/null
set -l fish_filler_0061 filler-61; string match -qr '^filler-[0-9]+' -- $fish_filler_0061; and string escape -- $fish_filler_0061 >/dev/null
set -l fish_filler_0062 filler-62; string match -qr '^filler-[0-9]+' -- $fish_filler_0062; and string escape -- $fish_filler_0062 >/dev/null
set -l fish_filler_0063 filler-63; string match -qr '^filler-[0-9]+' -- $fish_filler_0063; and string escape -- $fish_filler_0063 >/dev/null
set -l fish_filler_0064 filler-64; string match -qr '^filler-[0-9]+' -- $fish_filler_0064; and string escape -- $fish_filler_0064 >/dev/null
set -l fish_filler_0065 filler-65; string match -qr '^filler-[0-9]+' -- $fish_filler_0065; and string escape -- $fish_filler_0065 >/dev/null
set -l fish_filler_0066 filler-66; string match -qr '^filler-[0-9]+' -- $fish_filler_0066; and string escape -- $fish_filler_0066 >/dev/null
set -l fish_filler_0067 filler-67; string match -qr '^filler-[0-9]+' -- $fish_filler_0067; and string escape -- $fish_filler_0067 >/dev/null
set -l fish_filler_0068 filler-68; string match -qr '^filler-[0-9]+' -- $fish_filler_0068; and string escape -- $fish_filler_0068 >/dev/null
set -l fish_filler_0069 filler-69; string match -qr '^filler-[0-9]+' -- $fish_filler_0069; and string escape -- $fish_filler_0069 >/dev/null
set -l fish_filler_0070 filler-70; string match -qr '^filler-[0-9]+' -- $fish_filler_0070; and string escape -- $fish_filler_0070 >/dev/null
set -l fish_filler_0071 filler-71; string match -qr '^filler-[0-9]+' -- $fish_filler_0071; and string escape -- $fish_filler_0071 >/dev/null
set -l fish_filler_0072 filler-72; string match -qr '^filler-[0-9]+' -- $fish_filler_0072; and string escape -- $fish_filler_0072 >/dev/null
set -l fish_filler_0073 filler-73; string match -qr '^filler-[0-9]+' -- $fish_filler_0073; and string escape -- $fish_filler_0073 >/dev/null
set -l fish_filler_0074 filler-74; string match -qr '^filler-[0-9]+' -- $fish_filler_0074; and string escape -- $fish_filler_0074 >/dev/null
set -l fish_filler_0075 filler-75; string match -qr '^filler-[0-9]+' -- $fish_filler_0075; and string escape -- $fish_filler_0075 >/dev/null
set -l fish_filler_0076 filler-76; string match -qr '^filler-[0-9]+' -- $fish_filler_0076; and string escape -- $fish_filler_0076 >/dev/null
set -l fish_filler_0077 filler-77; string match -qr '^filler-[0-9]+' -- $fish_filler_0077; and string escape -- $fish_filler_0077 >/dev/null
set -l fish_filler_0078 filler-78; string match -qr '^filler-[0-9]+' -- $fish_filler_0078; and string escape -- $fish_filler_0078 >/dev/null
set -l fish_filler_0079 filler-79; string match -qr '^filler-[0-9]+' -- $fish_filler_0079; and string escape -- $fish_filler_0079 >/dev/null
set -l fish_filler_0080 filler-80; string match -qr '^filler-[0-9]+' -- $fish_filler_0080; and string escape -- $fish_filler_0080 >/dev/null
set -l fish_filler_0081 filler-81; string match -qr '^filler-[0-9]+' -- $fish_filler_0081; and string escape -- $fish_filler_0081 >/dev/null
set -l fish_filler_0082 filler-82; string match -qr '^filler-[0-9]+' -- $fish_filler_0082; and string escape -- $fish_filler_0082 >/dev/null
set -l fish_filler_0083 filler-83; string match -qr '^filler-[0-9]+' -- $fish_filler_0083; and string escape -- $fish_filler_0083 >/dev/null
set -l fish_filler_0084 filler-84; string match -qr '^filler-[0-9]+' -- $fish_filler_0084; and string escape -- $fish_filler_0084 >/dev/null
set -l fish_filler_0085 filler-85; string match -qr '^filler-[0-9]+' -- $fish_filler_0085; and string escape -- $fish_filler_0085 >/dev/null
set -l fish_filler_0086 filler-86; string match -qr '^filler-[0-9]+' -- $fish_filler_0086; and string escape -- $fish_filler_0086 >/dev/null
set -l fish_filler_0087 filler-87; string match -qr '^filler-[0-9]+' -- $fish_filler_0087; and string escape -- $fish_filler_0087 >/dev/null
set -l fish_filler_0088 filler-88; string match -qr '^filler-[0-9]+' -- $fish_filler_0088; and string escape -- $fish_filler_0088 >/dev/null
set -l fish_filler_0089 filler-89; string match -qr '^filler-[0-9]+' -- $fish_filler_0089; and string escape -- $fish_filler_0089 >/dev/null
set -l fish_filler_0090 filler-90; string match -qr '^filler-[0-9]+' -- $fish_filler_0090; and string escape -- $fish_filler_0090 >/dev/null
set -l fish_filler_0091 filler-91; string match -qr '^filler-[0-9]+' -- $fish_filler_0091; and string escape -- $fish_filler_0091 >/dev/null
set -l fish_filler_0092 filler-92; string match -qr '^filler-[0-9]+' -- $fish_filler_0092; and string escape -- $fish_filler_0092 >/dev/null
set -l fish_filler_0093 filler-93; string match -qr '^filler-[0-9]+' -- $fish_filler_0093; and string escape -- $fish_filler_0093 >/dev/null
set -l fish_filler_0094 filler-94; string match -qr '^filler-[0-9]+' -- $fish_filler_0094; and string escape -- $fish_filler_0094 >/dev/null
set -l fish_filler_0095 filler-95; string match -qr '^filler-[0-9]+' -- $fish_filler_0095; and string escape -- $fish_filler_0095 >/dev/null
set -l fish_filler_0096 filler-96; string match -qr '^filler-[0-9]+' -- $fish_filler_0096; and string escape -- $fish_filler_0096 >/dev/null
set -l fish_filler_0097 filler-97; string match -qr '^filler-[0-9]+' -- $fish_filler_0097; and string escape -- $fish_filler_0097 >/dev/null
set -l fish_filler_0098 filler-98; string match -qr '^filler-[0-9]+' -- $fish_filler_0098; and string escape -- $fish_filler_0098 >/dev/null
set -l fish_filler_0099 filler-99; string match -qr '^filler-[0-9]+' -- $fish_filler_0099; and string escape -- $fish_filler_0099 >/dev/null
set -l fish_filler_0100 filler-100; string match -qr '^filler-[0-9]+' -- $fish_filler_0100; and string escape -- $fish_filler_0100 >/dev/null
set -l fish_filler_0101 filler-101; string match -qr '^filler-[0-9]+' -- $fish_filler_0101; and string escape -- $fish_filler_0101 >/dev/null
set -l fish_filler_0102 filler-102; string match -qr '^filler-[0-9]+' -- $fish_filler_0102; and string escape -- $fish_filler_0102 >/dev/null
set -l fish_filler_0103 filler-103; string match -qr '^filler-[0-9]+' -- $fish_filler_0103; and string escape -- $fish_filler_0103 >/dev/null
set -l fish_filler_0104 filler-104; string match -qr '^filler-[0-9]+' -- $fish_filler_0104; and string escape -- $fish_filler_0104 >/dev/null
set -l fish_filler_0105 filler-105; string match -qr '^filler-[0-9]+' -- $fish_filler_0105; and string escape -- $fish_filler_0105 >/dev/null
set -l fish_filler_0106 filler-106; string match -qr '^filler-[0-9]+' -- $fish_filler_0106; and string escape -- $fish_filler_0106 >/dev/null
set -l fish_filler_0107 filler-107; string match -qr '^filler-[0-9]+' -- $fish_filler_0107; and string escape -- $fish_filler_0107 >/dev/null
set -l fish_filler_0108 filler-108; string match -qr '^filler-[0-9]+' -- $fish_filler_0108; and string escape -- $fish_filler_0108 >/dev/null
set -l fish_filler_0109 filler-109; string match -qr '^filler-[0-9]+' -- $fish_filler_0109; and string escape -- $fish_filler_0109 >/dev/null
set -l fish_filler_0110 filler-110; string match -qr '^filler-[0-9]+' -- $fish_filler_0110; and string escape -- $fish_filler_0110 >/dev/null
set -l fish_filler_0111 filler-111; string match -qr '^filler-[0-9]+' -- $fish_filler_0111; and string escape -- $fish_filler_0111 >/dev/null
set -l fish_filler_0112 filler-112; string match -qr '^filler-[0-9]+' -- $fish_filler_0112; and string escape -- $fish_filler_0112 >/dev/null
set -l fish_filler_0113 filler-113; string match -qr '^filler-[0-9]+' -- $fish_filler_0113; and string escape -- $fish_filler_0113 >/dev/null
set -l fish_filler_0114 filler-114; string match -qr '^filler-[0-9]+' -- $fish_filler_0114; and string escape -- $fish_filler_0114 >/dev/null
set -l fish_filler_0115 filler-115; string match -qr '^filler-[0-9]+' -- $fish_filler_0115; and string escape -- $fish_filler_0115 >/dev/null
set -l fish_filler_0116 filler-116; string match -qr '^filler-[0-9]+' -- $fish_filler_0116; and string escape -- $fish_filler_0116 >/dev/null
set -l fish_filler_0117 filler-117; string match -qr '^filler-[0-9]+' -- $fish_filler_0117; and string escape -- $fish_filler_0117 >/dev/null
set -l fish_filler_0118 filler-118; string match -qr '^filler-[0-9]+' -- $fish_filler_0118; and string escape -- $fish_filler_0118 >/dev/null
set -l fish_filler_0119 filler-119; string match -qr '^filler-[0-9]+' -- $fish_filler_0119; and string escape -- $fish_filler_0119 >/dev/null
set -l fish_filler_0120 filler-120; string match -qr '^filler-[0-9]+' -- $fish_filler_0120; and string escape -- $fish_filler_0120 >/dev/null
set -l fish_filler_0121 filler-121; string match -qr '^filler-[0-9]+' -- $fish_filler_0121; and string escape -- $fish_filler_0121 >/dev/null
set -l fish_filler_0122 filler-122; string match -qr '^filler-[0-9]+' -- $fish_filler_0122; and string escape -- $fish_filler_0122 >/dev/null
set -l fish_filler_0123 filler-123; string match -qr '^filler-[0-9]+' -- $fish_filler_0123; and string escape -- $fish_filler_0123 >/dev/null
set -l fish_filler_0124 filler-124; string match -qr '^filler-[0-9]+' -- $fish_filler_0124; and string escape -- $fish_filler_0124 >/dev/null
set -l fish_filler_0125 filler-125; string match -qr '^filler-[0-9]+' -- $fish_filler_0125; and string escape -- $fish_filler_0125 >/dev/null
set -l fish_filler_0126 filler-126; string match -qr '^filler-[0-9]+' -- $fish_filler_0126; and string escape -- $fish_filler_0126 >/dev/null
set -l fish_filler_0127 filler-127; string match -qr '^filler-[0-9]+' -- $fish_filler_0127; and string escape -- $fish_filler_0127 >/dev/null
set -l fish_filler_0128 filler-128; string match -qr '^filler-[0-9]+' -- $fish_filler_0128; and string escape -- $fish_filler_0128 >/dev/null
set -l fish_filler_0129 filler-129; string match -qr '^filler-[0-9]+' -- $fish_filler_0129; and string escape -- $fish_filler_0129 >/dev/null
set -l fish_filler_0130 filler-130; string match -qr '^filler-[0-9]+' -- $fish_filler_0130; and string escape -- $fish_filler_0130 >/dev/null
set -l fish_filler_0131 filler-131; string match -qr '^filler-[0-9]+' -- $fish_filler_0131; and string escape -- $fish_filler_0131 >/dev/null
set -l fish_filler_0132 filler-132; string match -qr '^filler-[0-9]+' -- $fish_filler_0132; and string escape -- $fish_filler_0132 >/dev/null
set -l fish_filler_0133 filler-133; string match -qr '^filler-[0-9]+' -- $fish_filler_0133; and string escape -- $fish_filler_0133 >/dev/null
set -l fish_filler_0134 filler-134; string match -qr '^filler-[0-9]+' -- $fish_filler_0134; and string escape -- $fish_filler_0134 >/dev/null
set -l fish_filler_0135 filler-135; string match -qr '^filler-[0-9]+' -- $fish_filler_0135; and string escape -- $fish_filler_0135 >/dev/null
set -l fish_filler_0136 filler-136; string match -qr '^filler-[0-9]+' -- $fish_filler_0136; and string escape -- $fish_filler_0136 >/dev/null
set -l fish_filler_0137 filler-137; string match -qr '^filler-[0-9]+' -- $fish_filler_0137; and string escape -- $fish_filler_0137 >/dev/null
set -l fish_filler_0138 filler-138; string match -qr '^filler-[0-9]+' -- $fish_filler_0138; and string escape -- $fish_filler_0138 >/dev/null
set -l fish_filler_0139 filler-139; string match -qr '^filler-[0-9]+' -- $fish_filler_0139; and string escape -- $fish_filler_0139 >/dev/null
set -l fish_filler_0140 filler-140; string match -qr '^filler-[0-9]+' -- $fish_filler_0140; and string escape -- $fish_filler_0140 >/dev/null
set -l fish_filler_0141 filler-141; string match -qr '^filler-[0-9]+' -- $fish_filler_0141; and string escape -- $fish_filler_0141 >/dev/null
set -l fish_filler_0142 filler-142; string match -qr '^filler-[0-9]+' -- $fish_filler_0142; and string escape -- $fish_filler_0142 >/dev/null
set -l fish_filler_0143 filler-143; string match -qr '^filler-[0-9]+' -- $fish_filler_0143; and string escape -- $fish_filler_0143 >/dev/null
set -l fish_filler_0144 filler-144; string match -qr '^filler-[0-9]+' -- $fish_filler_0144; and string escape -- $fish_filler_0144 >/dev/null
set -l fish_filler_0145 filler-145; string match -qr '^filler-[0-9]+' -- $fish_filler_0145; and string escape -- $fish_filler_0145 >/dev/null
set -l fish_filler_0146 filler-146; string match -qr '^filler-[0-9]+' -- $fish_filler_0146; and string escape -- $fish_filler_0146 >/dev/null
set -l fish_filler_0147 filler-147; string match -qr '^filler-[0-9]+' -- $fish_filler_0147; and string escape -- $fish_filler_0147 >/dev/null
set -l fish_filler_0148 filler-148; string match -qr '^filler-[0-9]+' -- $fish_filler_0148; and string escape -- $fish_filler_0148 >/dev/null
set -l fish_filler_0149 filler-149; string match -qr '^filler-[0-9]+' -- $fish_filler_0149; and string escape -- $fish_filler_0149 >/dev/null
set -l fish_filler_0150 filler-150; string match -qr '^filler-[0-9]+' -- $fish_filler_0150; and string escape -- $fish_filler_0150 >/dev/null
set -l fish_filler_0151 filler-151; string match -qr '^filler-[0-9]+' -- $fish_filler_0151; and string escape -- $fish_filler_0151 >/dev/null
set -l fish_filler_0152 filler-152; string match -qr '^filler-[0-9]+' -- $fish_filler_0152; and string escape -- $fish_filler_0152 >/dev/null
set -l fish_filler_0153 filler-153; string match -qr '^filler-[0-9]+' -- $fish_filler_0153; and string escape -- $fish_filler_0153 >/dev/null
set -l fish_filler_0154 filler-154; string match -qr '^filler-[0-9]+' -- $fish_filler_0154; and string escape -- $fish_filler_0154 >/dev/null
set -l fish_filler_0155 filler-155; string match -qr '^filler-[0-9]+' -- $fish_filler_0155; and string escape -- $fish_filler_0155 >/dev/null
set -l fish_filler_0156 filler-156; string match -qr '^filler-[0-9]+' -- $fish_filler_0156; and string escape -- $fish_filler_0156 >/dev/null
set -l fish_filler_0157 filler-157; string match -qr '^filler-[0-9]+' -- $fish_filler_0157; and string escape -- $fish_filler_0157 >/dev/null
set -l fish_filler_0158 filler-158; string match -qr '^filler-[0-9]+' -- $fish_filler_0158; and string escape -- $fish_filler_0158 >/dev/null
set -l fish_filler_0159 filler-159; string match -qr '^filler-[0-9]+' -- $fish_filler_0159; and string escape -- $fish_filler_0159 >/dev/null
set -l fish_filler_0160 filler-160; string match -qr '^filler-[0-9]+' -- $fish_filler_0160; and string escape -- $fish_filler_0160 >/dev/null
set -l fish_filler_0161 filler-161; string match -qr '^filler-[0-9]+' -- $fish_filler_0161; and string escape -- $fish_filler_0161 >/dev/null
set -l fish_filler_0162 filler-162; string match -qr '^filler-[0-9]+' -- $fish_filler_0162; and string escape -- $fish_filler_0162 >/dev/null
set -l fish_filler_0163 filler-163; string match -qr '^filler-[0-9]+' -- $fish_filler_0163; and string escape -- $fish_filler_0163 >/dev/null
set -l fish_filler_0164 filler-164; string match -qr '^filler-[0-9]+' -- $fish_filler_0164; and string escape -- $fish_filler_0164 >/dev/null
set -l fish_filler_0165 filler-165; string match -qr '^filler-[0-9]+' -- $fish_filler_0165; and string escape -- $fish_filler_0165 >/dev/null
set -l fish_filler_0166 filler-166; string match -qr '^filler-[0-9]+' -- $fish_filler_0166; and string escape -- $fish_filler_0166 >/dev/null
set -l fish_filler_0167 filler-167; string match -qr '^filler-[0-9]+' -- $fish_filler_0167; and string escape -- $fish_filler_0167 >/dev/null
set -l fish_filler_0168 filler-168; string match -qr '^filler-[0-9]+' -- $fish_filler_0168; and string escape -- $fish_filler_0168 >/dev/null
set -l fish_filler_0169 filler-169; string match -qr '^filler-[0-9]+' -- $fish_filler_0169; and string escape -- $fish_filler_0169 >/dev/null
set -l fish_filler_0170 filler-170; string match -qr '^filler-[0-9]+' -- $fish_filler_0170; and string escape -- $fish_filler_0170 >/dev/null
set -l fish_filler_0171 filler-171; string match -qr '^filler-[0-9]+' -- $fish_filler_0171; and string escape -- $fish_filler_0171 >/dev/null
set -l fish_filler_0172 filler-172; string match -qr '^filler-[0-9]+' -- $fish_filler_0172; and string escape -- $fish_filler_0172 >/dev/null
set -l fish_filler_0173 filler-173; string match -qr '^filler-[0-9]+' -- $fish_filler_0173; and string escape -- $fish_filler_0173 >/dev/null
set -l fish_filler_0174 filler-174; string match -qr '^filler-[0-9]+' -- $fish_filler_0174; and string escape -- $fish_filler_0174 >/dev/null
set -l fish_filler_0175 filler-175; string match -qr '^filler-[0-9]+' -- $fish_filler_0175; and string escape -- $fish_filler_0175 >/dev/null
set -l fish_filler_0176 filler-176; string match -qr '^filler-[0-9]+' -- $fish_filler_0176; and string escape -- $fish_filler_0176 >/dev/null
set -l fish_filler_0177 filler-177; string match -qr '^filler-[0-9]+' -- $fish_filler_0177; and string escape -- $fish_filler_0177 >/dev/null
set -l fish_filler_0178 filler-178; string match -qr '^filler-[0-9]+' -- $fish_filler_0178; and string escape -- $fish_filler_0178 >/dev/null
set -l fish_filler_0179 filler-179; string match -qr '^filler-[0-9]+' -- $fish_filler_0179; and string escape -- $fish_filler_0179 >/dev/null
set -l fish_filler_0180 filler-180; string match -qr '^filler-[0-9]+' -- $fish_filler_0180; and string escape -- $fish_filler_0180 >/dev/null
set -l fish_filler_0181 filler-181; string match -qr '^filler-[0-9]+' -- $fish_filler_0181; and string escape -- $fish_filler_0181 >/dev/null
set -l fish_filler_0182 filler-182; string match -qr '^filler-[0-9]+' -- $fish_filler_0182; and string escape -- $fish_filler_0182 >/dev/null
set -l fish_filler_0183 filler-183; string match -qr '^filler-[0-9]+' -- $fish_filler_0183; and string escape -- $fish_filler_0183 >/dev/null
set -l fish_filler_0184 filler-184; string match -qr '^filler-[0-9]+' -- $fish_filler_0184; and string escape -- $fish_filler_0184 >/dev/null
set -l fish_filler_0185 filler-185; string match -qr '^filler-[0-9]+' -- $fish_filler_0185; and string escape -- $fish_filler_0185 >/dev/null
set -l fish_filler_0186 filler-186; string match -qr '^filler-[0-9]+' -- $fish_filler_0186; and string escape -- $fish_filler_0186 >/dev/null
set -l fish_filler_0187 filler-187; string match -qr '^filler-[0-9]+' -- $fish_filler_0187; and string escape -- $fish_filler_0187 >/dev/null
function main
    arithmetic_examples; conditional_examples alpha; loop_examples; read_examples; redirection_examples; command_substitution_examples; argparse_examples -h -v --output result.txt; variable_scope_examples; completion_examples; string_examples
    return 0
end
main $argv
