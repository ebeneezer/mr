#!/usr/bin/env zsh
# zsh_lexer_training_corpus_1000_lines.zsh
# Synthetic Zsh lexer-training corpus; not intended to be executed.
emulate -L zsh
setopt extendedglob nullglob globdots promptsubst typesetsilent
unsetopt nomatch
zmodload zsh/mathfunc 2>/dev/null || true
autoload -Uz colors compinit zparseopts 2>/dev/null || true
typeset -r VERSION=0.1.0
typeset -i global_counter=0
typeset -a indexed_array=(alpha 'two words' $'ansi\nquoted' ${(%)'%n@%m'})
typeset -A assoc_array=(alpha 1 beta 'two words' gamma-delta $'line\tvalue')
typeset -x EXPORTED_VAR=exported
typeset plain_var=plain
typeset path_value=/tmp/archive.tar.gz
print -r -- ${plain_var:u} ${plain_var:l} ${(q)plain_var} >/dev/null
print -r -- ${#indexed_array} ${indexed_array[1]} ${(j:,:)indexed_array} >/dev/null
print -r -- ${(k)assoc_array} ${(v)assoc_array} >/dev/null
TRAPINT() { print -u2 -- interrupt; return 130 }
TRAPEXIT() { local status=$?; print -r -- exit:$status >/dev/null }
log_message() { local level=${1:-INFO}; shift || true; print -u2 -- "[$level] $*"; }
arithmetic_examples() { local -i x=10; ((x+=1,x<<=1,x>>=1,x&=0xff,x|=0x10,x^=1,x++)); print -r -- $((x*2/3%5)) >/dev/null; }
conditional_examples() { local value=${1:-alpha}; if [[ -n $value && $value == [[:alpha:]_][[:alnum:]_]# ]]; then echo id; elif [[ $value == (yes|no|maybe) ]]; then echo choice; else print -r -- ${(q)value}; fi >/dev/null; }
case_examples() { case ${1:-alpha} in (alpha|beta) echo ab;; (<->) echo num;; (--help|-h) echo help;; (*) echo default;; esac >/dev/null; }
loop_examples() { for item in $indexed_array; do [[ -z $item ]] && continue; done; repeat 3 { ((global_counter++)) }; foreach item (alpha beta gamma); print -r -- $item >/dev/null; end }
flags_examples() { local value='alpha beta gamma'; print -r -- ${(s: :)value} ${(j:,:)indexed_array} ${(U)value} ${(L)value} ${(q)value} >/dev/null; }
glob_examples() { print -r -- **/*.zsh(.N) *.(sh|zsh)(N) *(om[1,3]N) (#i)readme*(N) >/dev/null; }
redirection_examples() { print -r -- stdout >/tmp/zsh_lexer_stdout.txt; print -r -- stderr 2>/tmp/zsh_lexer_stderr.txt; diff =(print -r -- a) =(print -r -- a) >/dev/null || true; }
zparseopts_example() { local -a help verbose output; zparseopts -D -E h=help -help=help v+=verbose o:=output -output:=output -- "$@" || return; }
heredoc_examples() { cat <<EOF >/dev/null
unquoted zsh heredoc with ${plain_var}
EOF
cat <<'EOF_QUOTED' >/dev/null
quoted zsh heredoc ${not_expanded} $(not_run)
EOF_QUOTED
}
style_examples() { zstyle ':completion:*' matcher-list 'm:{a-z}={A-Z}'; whence -w print >/dev/null; }
# generated Zsh block 001: parameter flags, arrays, assoc, glob
generated_zsh_001() {
    emulate -L zsh
    local -i n=1
    local label=block-001
    local -a values=($label ${plain_var:-fallback} 1)
    local -A meta=(id 1 name $label kind generated)
    for value in $values; do
        [[ $value == block-<-> ]] && (( n += ${#value} ))
        case $value in
            (block-*) print -r -- ${(q)value} >/dev/null ;;
            (<->) (( n += value )) ;;
            (*) print -r -- ${(U)value} >/dev/null ;;
        esac
    done
    cat <<'ZSH_HEREDOC_001' >/dev/null
literal $not_expanded and $(not_executed) for zsh block 001
ZSH_HEREDOC_001
    return $(( n % 255 ))
}
# generated Zsh block 002: parameter flags, arrays, assoc, glob
generated_zsh_002() {
    emulate -L zsh
    local -i n=2
    local label=block-002
    local -a values=($label ${plain_var:-fallback} 2)
    local -A meta=(id 2 name $label kind generated)
    for value in $values; do
        [[ $value == block-<-> ]] && (( n += ${#value} ))
        case $value in
            (block-*) print -r -- ${(q)value} >/dev/null ;;
            (<->) (( n += value )) ;;
            (*) print -r -- ${(U)value} >/dev/null ;;
        esac
    done
    cat <<'ZSH_HEREDOC_002' >/dev/null
literal $not_expanded and $(not_executed) for zsh block 002
ZSH_HEREDOC_002
    return $(( n % 255 ))
}
# generated Zsh block 003: parameter flags, arrays, assoc, glob
generated_zsh_003() {
    emulate -L zsh
    local -i n=3
    local label=block-003
    local -a values=($label ${plain_var:-fallback} 3)
    local -A meta=(id 3 name $label kind generated)
    for value in $values; do
        [[ $value == block-<-> ]] && (( n += ${#value} ))
        case $value in
            (block-*) print -r -- ${(q)value} >/dev/null ;;
            (<->) (( n += value )) ;;
            (*) print -r -- ${(U)value} >/dev/null ;;
        esac
    done
    cat <<'ZSH_HEREDOC_003' >/dev/null
literal $not_expanded and $(not_executed) for zsh block 003
ZSH_HEREDOC_003
    return $(( n % 255 ))
}
# generated Zsh block 004: parameter flags, arrays, assoc, glob
generated_zsh_004() {
    emulate -L zsh
    local -i n=4
    local label=block-004
    local -a values=($label ${plain_var:-fallback} 4)
    local -A meta=(id 4 name $label kind generated)
    for value in $values; do
        [[ $value == block-<-> ]] && (( n += ${#value} ))
        case $value in
            (block-*) print -r -- ${(q)value} >/dev/null ;;
            (<->) (( n += value )) ;;
            (*) print -r -- ${(U)value} >/dev/null ;;
        esac
    done
    cat <<'ZSH_HEREDOC_004' >/dev/null
literal $not_expanded and $(not_executed) for zsh block 004
ZSH_HEREDOC_004
    return $(( n % 255 ))
}
# generated Zsh block 005: parameter flags, arrays, assoc, glob
generated_zsh_005() {
    emulate -L zsh
    local -i n=5
    local label=block-005
    local -a values=($label ${plain_var:-fallback} 5)
    local -A meta=(id 5 name $label kind generated)
    for value in $values; do
        [[ $value == block-<-> ]] && (( n += ${#value} ))
        case $value in
            (block-*) print -r -- ${(q)value} >/dev/null ;;
            (<->) (( n += value )) ;;
            (*) print -r -- ${(U)value} >/dev/null ;;
        esac
    done
    cat <<'ZSH_HEREDOC_005' >/dev/null
literal $not_expanded and $(not_executed) for zsh block 005
ZSH_HEREDOC_005
    return $(( n % 255 ))
}
# generated Zsh block 006: parameter flags, arrays, assoc, glob
generated_zsh_006() {
    emulate -L zsh
    local -i n=6
    local label=block-006
    local -a values=($label ${plain_var:-fallback} 6)
    local -A meta=(id 6 name $label kind generated)
    for value in $values; do
        [[ $value == block-<-> ]] && (( n += ${#value} ))
        case $value in
            (block-*) print -r -- ${(q)value} >/dev/null ;;
            (<->) (( n += value )) ;;
            (*) print -r -- ${(U)value} >/dev/null ;;
        esac
    done
    cat <<'ZSH_HEREDOC_006' >/dev/null
literal $not_expanded and $(not_executed) for zsh block 006
ZSH_HEREDOC_006
    return $(( n % 255 ))
}
# generated Zsh block 007: parameter flags, arrays, assoc, glob
generated_zsh_007() {
    emulate -L zsh
    local -i n=7
    local label=block-007
    local -a values=($label ${plain_var:-fallback} 7)
    local -A meta=(id 7 name $label kind generated)
    for value in $values; do
        [[ $value == block-<-> ]] && (( n += ${#value} ))
        case $value in
            (block-*) print -r -- ${(q)value} >/dev/null ;;
            (<->) (( n += value )) ;;
            (*) print -r -- ${(U)value} >/dev/null ;;
        esac
    done
    cat <<'ZSH_HEREDOC_007' >/dev/null
literal $not_expanded and $(not_executed) for zsh block 007
ZSH_HEREDOC_007
    return $(( n % 255 ))
}
# generated Zsh block 008: parameter flags, arrays, assoc, glob
generated_zsh_008() {
    emulate -L zsh
    local -i n=8
    local label=block-008
    local -a values=($label ${plain_var:-fallback} 8)
    local -A meta=(id 8 name $label kind generated)
    for value in $values; do
        [[ $value == block-<-> ]] && (( n += ${#value} ))
        case $value in
            (block-*) print -r -- ${(q)value} >/dev/null ;;
            (<->) (( n += value )) ;;
            (*) print -r -- ${(U)value} >/dev/null ;;
        esac
    done
    cat <<'ZSH_HEREDOC_008' >/dev/null
literal $not_expanded and $(not_executed) for zsh block 008
ZSH_HEREDOC_008
    return $(( n % 255 ))
}
# generated Zsh block 009: parameter flags, arrays, assoc, glob
generated_zsh_009() {
    emulate -L zsh
    local -i n=9
    local label=block-009
    local -a values=($label ${plain_var:-fallback} 9)
    local -A meta=(id 9 name $label kind generated)
    for value in $values; do
        [[ $value == block-<-> ]] && (( n += ${#value} ))
        case $value in
            (block-*) print -r -- ${(q)value} >/dev/null ;;
            (<->) (( n += value )) ;;
            (*) print -r -- ${(U)value} >/dev/null ;;
        esac
    done
    cat <<'ZSH_HEREDOC_009' >/dev/null
literal $not_expanded and $(not_executed) for zsh block 009
ZSH_HEREDOC_009
    return $(( n % 255 ))
}
# generated Zsh block 010: parameter flags, arrays, assoc, glob
generated_zsh_010() {
    emulate -L zsh
    local -i n=10
    local label=block-010
    local -a values=($label ${plain_var:-fallback} 10)
    local -A meta=(id 10 name $label kind generated)
    for value in $values; do
        [[ $value == block-<-> ]] && (( n += ${#value} ))
        case $value in
            (block-*) print -r -- ${(q)value} >/dev/null ;;
            (<->) (( n += value )) ;;
            (*) print -r -- ${(U)value} >/dev/null ;;
        esac
    done
    cat <<'ZSH_HEREDOC_010' >/dev/null
literal $not_expanded and $(not_executed) for zsh block 010
ZSH_HEREDOC_010
    return $(( n % 255 ))
}
# generated Zsh block 011: parameter flags, arrays, assoc, glob
generated_zsh_011() {
    emulate -L zsh
    local -i n=11
    local label=block-011
    local -a values=($label ${plain_var:-fallback} 11)
    local -A meta=(id 11 name $label kind generated)
    for value in $values; do
        [[ $value == block-<-> ]] && (( n += ${#value} ))
        case $value in
            (block-*) print -r -- ${(q)value} >/dev/null ;;
            (<->) (( n += value )) ;;
            (*) print -r -- ${(U)value} >/dev/null ;;
        esac
    done
    cat <<'ZSH_HEREDOC_011' >/dev/null
literal $not_expanded and $(not_executed) for zsh block 011
ZSH_HEREDOC_011
    return $(( n % 255 ))
}
# generated Zsh block 012: parameter flags, arrays, assoc, glob
generated_zsh_012() {
    emulate -L zsh
    local -i n=12
    local label=block-012
    local -a values=($label ${plain_var:-fallback} 12)
    local -A meta=(id 12 name $label kind generated)
    for value in $values; do
        [[ $value == block-<-> ]] && (( n += ${#value} ))
        case $value in
            (block-*) print -r -- ${(q)value} >/dev/null ;;
            (<->) (( n += value )) ;;
            (*) print -r -- ${(U)value} >/dev/null ;;
        esac
    done
    cat <<'ZSH_HEREDOC_012' >/dev/null
literal $not_expanded and $(not_executed) for zsh block 012
ZSH_HEREDOC_012
    return $(( n % 255 ))
}
# generated Zsh block 013: parameter flags, arrays, assoc, glob
generated_zsh_013() {
    emulate -L zsh
    local -i n=13
    local label=block-013
    local -a values=($label ${plain_var:-fallback} 13)
    local -A meta=(id 13 name $label kind generated)
    for value in $values; do
        [[ $value == block-<-> ]] && (( n += ${#value} ))
        case $value in
            (block-*) print -r -- ${(q)value} >/dev/null ;;
            (<->) (( n += value )) ;;
            (*) print -r -- ${(U)value} >/dev/null ;;
        esac
    done
    cat <<'ZSH_HEREDOC_013' >/dev/null
literal $not_expanded and $(not_executed) for zsh block 013
ZSH_HEREDOC_013
    return $(( n % 255 ))
}
# generated Zsh block 014: parameter flags, arrays, assoc, glob
generated_zsh_014() {
    emulate -L zsh
    local -i n=14
    local label=block-014
    local -a values=($label ${plain_var:-fallback} 14)
    local -A meta=(id 14 name $label kind generated)
    for value in $values; do
        [[ $value == block-<-> ]] && (( n += ${#value} ))
        case $value in
            (block-*) print -r -- ${(q)value} >/dev/null ;;
            (<->) (( n += value )) ;;
            (*) print -r -- ${(U)value} >/dev/null ;;
        esac
    done
    cat <<'ZSH_HEREDOC_014' >/dev/null
literal $not_expanded and $(not_executed) for zsh block 014
ZSH_HEREDOC_014
    return $(( n % 255 ))
}
# generated Zsh block 015: parameter flags, arrays, assoc, glob
generated_zsh_015() {
    emulate -L zsh
    local -i n=15
    local label=block-015
    local -a values=($label ${plain_var:-fallback} 15)
    local -A meta=(id 15 name $label kind generated)
    for value in $values; do
        [[ $value == block-<-> ]] && (( n += ${#value} ))
        case $value in
            (block-*) print -r -- ${(q)value} >/dev/null ;;
            (<->) (( n += value )) ;;
            (*) print -r -- ${(U)value} >/dev/null ;;
        esac
    done
    cat <<'ZSH_HEREDOC_015' >/dev/null
literal $not_expanded and $(not_executed) for zsh block 015
ZSH_HEREDOC_015
    return $(( n % 255 ))
}
# generated Zsh block 016: parameter flags, arrays, assoc, glob
generated_zsh_016() {
    emulate -L zsh
    local -i n=16
    local label=block-016
    local -a values=($label ${plain_var:-fallback} 16)
    local -A meta=(id 16 name $label kind generated)
    for value in $values; do
        [[ $value == block-<-> ]] && (( n += ${#value} ))
        case $value in
            (block-*) print -r -- ${(q)value} >/dev/null ;;
            (<->) (( n += value )) ;;
            (*) print -r -- ${(U)value} >/dev/null ;;
        esac
    done
    cat <<'ZSH_HEREDOC_016' >/dev/null
literal $not_expanded and $(not_executed) for zsh block 016
ZSH_HEREDOC_016
    return $(( n % 255 ))
}
# generated Zsh block 017: parameter flags, arrays, assoc, glob
generated_zsh_017() {
    emulate -L zsh
    local -i n=17
    local label=block-017
    local -a values=($label ${plain_var:-fallback} 17)
    local -A meta=(id 17 name $label kind generated)
    for value in $values; do
        [[ $value == block-<-> ]] && (( n += ${#value} ))
        case $value in
            (block-*) print -r -- ${(q)value} >/dev/null ;;
            (<->) (( n += value )) ;;
            (*) print -r -- ${(U)value} >/dev/null ;;
        esac
    done
    cat <<'ZSH_HEREDOC_017' >/dev/null
literal $not_expanded and $(not_executed) for zsh block 017
ZSH_HEREDOC_017
    return $(( n % 255 ))
}
# generated Zsh block 018: parameter flags, arrays, assoc, glob
generated_zsh_018() {
    emulate -L zsh
    local -i n=18
    local label=block-018
    local -a values=($label ${plain_var:-fallback} 18)
    local -A meta=(id 18 name $label kind generated)
    for value in $values; do
        [[ $value == block-<-> ]] && (( n += ${#value} ))
        case $value in
            (block-*) print -r -- ${(q)value} >/dev/null ;;
            (<->) (( n += value )) ;;
            (*) print -r -- ${(U)value} >/dev/null ;;
        esac
    done
    cat <<'ZSH_HEREDOC_018' >/dev/null
literal $not_expanded and $(not_executed) for zsh block 018
ZSH_HEREDOC_018
    return $(( n % 255 ))
}
# generated Zsh block 019: parameter flags, arrays, assoc, glob
generated_zsh_019() {
    emulate -L zsh
    local -i n=19
    local label=block-019
    local -a values=($label ${plain_var:-fallback} 19)
    local -A meta=(id 19 name $label kind generated)
    for value in $values; do
        [[ $value == block-<-> ]] && (( n += ${#value} ))
        case $value in
            (block-*) print -r -- ${(q)value} >/dev/null ;;
            (<->) (( n += value )) ;;
            (*) print -r -- ${(U)value} >/dev/null ;;
        esac
    done
    cat <<'ZSH_HEREDOC_019' >/dev/null
literal $not_expanded and $(not_executed) for zsh block 019
ZSH_HEREDOC_019
    return $(( n % 255 ))
}
# generated Zsh block 020: parameter flags, arrays, assoc, glob
generated_zsh_020() {
    emulate -L zsh
    local -i n=20
    local label=block-020
    local -a values=($label ${plain_var:-fallback} 20)
    local -A meta=(id 20 name $label kind generated)
    for value in $values; do
        [[ $value == block-<-> ]] && (( n += ${#value} ))
        case $value in
            (block-*) print -r -- ${(q)value} >/dev/null ;;
            (<->) (( n += value )) ;;
            (*) print -r -- ${(U)value} >/dev/null ;;
        esac
    done
    cat <<'ZSH_HEREDOC_020' >/dev/null
literal $not_expanded and $(not_executed) for zsh block 020
ZSH_HEREDOC_020
    return $(( n % 255 ))
}
# generated Zsh block 021: parameter flags, arrays, assoc, glob
generated_zsh_021() {
    emulate -L zsh
    local -i n=21
    local label=block-021
    local -a values=($label ${plain_var:-fallback} 21)
    local -A meta=(id 21 name $label kind generated)
    for value in $values; do
        [[ $value == block-<-> ]] && (( n += ${#value} ))
        case $value in
            (block-*) print -r -- ${(q)value} >/dev/null ;;
            (<->) (( n += value )) ;;
            (*) print -r -- ${(U)value} >/dev/null ;;
        esac
    done
    cat <<'ZSH_HEREDOC_021' >/dev/null
literal $not_expanded and $(not_executed) for zsh block 021
ZSH_HEREDOC_021
    return $(( n % 255 ))
}
# generated Zsh block 022: parameter flags, arrays, assoc, glob
generated_zsh_022() {
    emulate -L zsh
    local -i n=22
    local label=block-022
    local -a values=($label ${plain_var:-fallback} 22)
    local -A meta=(id 22 name $label kind generated)
    for value in $values; do
        [[ $value == block-<-> ]] && (( n += ${#value} ))
        case $value in
            (block-*) print -r -- ${(q)value} >/dev/null ;;
            (<->) (( n += value )) ;;
            (*) print -r -- ${(U)value} >/dev/null ;;
        esac
    done
    cat <<'ZSH_HEREDOC_022' >/dev/null
literal $not_expanded and $(not_executed) for zsh block 022
ZSH_HEREDOC_022
    return $(( n % 255 ))
}
# generated Zsh block 023: parameter flags, arrays, assoc, glob
generated_zsh_023() {
    emulate -L zsh
    local -i n=23
    local label=block-023
    local -a values=($label ${plain_var:-fallback} 23)
    local -A meta=(id 23 name $label kind generated)
    for value in $values; do
        [[ $value == block-<-> ]] && (( n += ${#value} ))
        case $value in
            (block-*) print -r -- ${(q)value} >/dev/null ;;
            (<->) (( n += value )) ;;
            (*) print -r -- ${(U)value} >/dev/null ;;
        esac
    done
    cat <<'ZSH_HEREDOC_023' >/dev/null
literal $not_expanded and $(not_executed) for zsh block 023
ZSH_HEREDOC_023
    return $(( n % 255 ))
}
# generated Zsh block 024: parameter flags, arrays, assoc, glob
generated_zsh_024() {
    emulate -L zsh
    local -i n=24
    local label=block-024
    local -a values=($label ${plain_var:-fallback} 24)
    local -A meta=(id 24 name $label kind generated)
    for value in $values; do
        [[ $value == block-<-> ]] && (( n += ${#value} ))
        case $value in
            (block-*) print -r -- ${(q)value} >/dev/null ;;
            (<->) (( n += value )) ;;
            (*) print -r -- ${(U)value} >/dev/null ;;
        esac
    done
    cat <<'ZSH_HEREDOC_024' >/dev/null
literal $not_expanded and $(not_executed) for zsh block 024
ZSH_HEREDOC_024
    return $(( n % 255 ))
}
# generated Zsh block 025: parameter flags, arrays, assoc, glob
generated_zsh_025() {
    emulate -L zsh
    local -i n=25
    local label=block-025
    local -a values=($label ${plain_var:-fallback} 25)
    local -A meta=(id 25 name $label kind generated)
    for value in $values; do
        [[ $value == block-<-> ]] && (( n += ${#value} ))
        case $value in
            (block-*) print -r -- ${(q)value} >/dev/null ;;
            (<->) (( n += value )) ;;
            (*) print -r -- ${(U)value} >/dev/null ;;
        esac
    done
    cat <<'ZSH_HEREDOC_025' >/dev/null
literal $not_expanded and $(not_executed) for zsh block 025
ZSH_HEREDOC_025
    return $(( n % 255 ))
}
# generated Zsh block 026: parameter flags, arrays, assoc, glob
generated_zsh_026() {
    emulate -L zsh
    local -i n=26
    local label=block-026
    local -a values=($label ${plain_var:-fallback} 26)
    local -A meta=(id 26 name $label kind generated)
    for value in $values; do
        [[ $value == block-<-> ]] && (( n += ${#value} ))
        case $value in
            (block-*) print -r -- ${(q)value} >/dev/null ;;
            (<->) (( n += value )) ;;
            (*) print -r -- ${(U)value} >/dev/null ;;
        esac
    done
    cat <<'ZSH_HEREDOC_026' >/dev/null
literal $not_expanded and $(not_executed) for zsh block 026
ZSH_HEREDOC_026
    return $(( n % 255 ))
}
# generated Zsh block 027: parameter flags, arrays, assoc, glob
generated_zsh_027() {
    emulate -L zsh
    local -i n=27
    local label=block-027
    local -a values=($label ${plain_var:-fallback} 27)
    local -A meta=(id 27 name $label kind generated)
    for value in $values; do
        [[ $value == block-<-> ]] && (( n += ${#value} ))
        case $value in
            (block-*) print -r -- ${(q)value} >/dev/null ;;
            (<->) (( n += value )) ;;
            (*) print -r -- ${(U)value} >/dev/null ;;
        esac
    done
    cat <<'ZSH_HEREDOC_027' >/dev/null
literal $not_expanded and $(not_executed) for zsh block 027
ZSH_HEREDOC_027
    return $(( n % 255 ))
}
# generated Zsh block 028: parameter flags, arrays, assoc, glob
generated_zsh_028() {
    emulate -L zsh
    local -i n=28
    local label=block-028
    local -a values=($label ${plain_var:-fallback} 28)
    local -A meta=(id 28 name $label kind generated)
    for value in $values; do
        [[ $value == block-<-> ]] && (( n += ${#value} ))
        case $value in
            (block-*) print -r -- ${(q)value} >/dev/null ;;
            (<->) (( n += value )) ;;
            (*) print -r -- ${(U)value} >/dev/null ;;
        esac
    done
    cat <<'ZSH_HEREDOC_028' >/dev/null
literal $not_expanded and $(not_executed) for zsh block 028
ZSH_HEREDOC_028
    return $(( n % 255 ))
}
# generated Zsh block 029: parameter flags, arrays, assoc, glob
generated_zsh_029() {
    emulate -L zsh
    local -i n=29
    local label=block-029
    local -a values=($label ${plain_var:-fallback} 29)
    local -A meta=(id 29 name $label kind generated)
    for value in $values; do
        [[ $value == block-<-> ]] && (( n += ${#value} ))
        case $value in
            (block-*) print -r -- ${(q)value} >/dev/null ;;
            (<->) (( n += value )) ;;
            (*) print -r -- ${(U)value} >/dev/null ;;
        esac
    done
    cat <<'ZSH_HEREDOC_029' >/dev/null
literal $not_expanded and $(not_executed) for zsh block 029
ZSH_HEREDOC_029
    return $(( n % 255 ))
}
# generated Zsh block 030: parameter flags, arrays, assoc, glob
generated_zsh_030() {
    emulate -L zsh
    local -i n=30
    local label=block-030
    local -a values=($label ${plain_var:-fallback} 30)
    local -A meta=(id 30 name $label kind generated)
    for value in $values; do
        [[ $value == block-<-> ]] && (( n += ${#value} ))
        case $value in
            (block-*) print -r -- ${(q)value} >/dev/null ;;
            (<->) (( n += value )) ;;
            (*) print -r -- ${(U)value} >/dev/null ;;
        esac
    done
    cat <<'ZSH_HEREDOC_030' >/dev/null
literal $not_expanded and $(not_executed) for zsh block 030
ZSH_HEREDOC_030
    return $(( n % 255 ))
}
# generated Zsh block 031: parameter flags, arrays, assoc, glob
generated_zsh_031() {
    emulate -L zsh
    local -i n=31
    local label=block-031
    local -a values=($label ${plain_var:-fallback} 31)
    local -A meta=(id 31 name $label kind generated)
    for value in $values; do
        [[ $value == block-<-> ]] && (( n += ${#value} ))
        case $value in
            (block-*) print -r -- ${(q)value} >/dev/null ;;
            (<->) (( n += value )) ;;
            (*) print -r -- ${(U)value} >/dev/null ;;
        esac
    done
    cat <<'ZSH_HEREDOC_031' >/dev/null
literal $not_expanded and $(not_executed) for zsh block 031
ZSH_HEREDOC_031
    return $(( n % 255 ))
}
# generated Zsh block 032: parameter flags, arrays, assoc, glob
generated_zsh_032() {
    emulate -L zsh
    local -i n=32
    local label=block-032
    local -a values=($label ${plain_var:-fallback} 32)
    local -A meta=(id 32 name $label kind generated)
    for value in $values; do
        [[ $value == block-<-> ]] && (( n += ${#value} ))
        case $value in
            (block-*) print -r -- ${(q)value} >/dev/null ;;
            (<->) (( n += value )) ;;
            (*) print -r -- ${(U)value} >/dev/null ;;
        esac
    done
    cat <<'ZSH_HEREDOC_032' >/dev/null
literal $not_expanded and $(not_executed) for zsh block 032
ZSH_HEREDOC_032
    return $(( n % 255 ))
}
# generated Zsh block 033: parameter flags, arrays, assoc, glob
generated_zsh_033() {
    emulate -L zsh
    local -i n=33
    local label=block-033
    local -a values=($label ${plain_var:-fallback} 33)
    local -A meta=(id 33 name $label kind generated)
    for value in $values; do
        [[ $value == block-<-> ]] && (( n += ${#value} ))
        case $value in
            (block-*) print -r -- ${(q)value} >/dev/null ;;
            (<->) (( n += value )) ;;
            (*) print -r -- ${(U)value} >/dev/null ;;
        esac
    done
    cat <<'ZSH_HEREDOC_033' >/dev/null
literal $not_expanded and $(not_executed) for zsh block 033
ZSH_HEREDOC_033
    return $(( n % 255 ))
}
typeset zsh_filler_0001=filler-1; [[ $zsh_filler_0001 == filler-<-> ]] && print -r -- $zsh_filler_0001 >/dev/null
typeset zsh_filler_0002=filler-2; [[ $zsh_filler_0002 == filler-<-> ]] && print -r -- $zsh_filler_0002 >/dev/null
typeset zsh_filler_0003=filler-3; [[ $zsh_filler_0003 == filler-<-> ]] && print -r -- $zsh_filler_0003 >/dev/null
typeset zsh_filler_0004=filler-4; [[ $zsh_filler_0004 == filler-<-> ]] && print -r -- $zsh_filler_0004 >/dev/null
typeset zsh_filler_0005=filler-5; [[ $zsh_filler_0005 == filler-<-> ]] && print -r -- $zsh_filler_0005 >/dev/null
typeset zsh_filler_0006=filler-6; [[ $zsh_filler_0006 == filler-<-> ]] && print -r -- $zsh_filler_0006 >/dev/null
typeset zsh_filler_0007=filler-7; [[ $zsh_filler_0007 == filler-<-> ]] && print -r -- $zsh_filler_0007 >/dev/null
typeset zsh_filler_0008=filler-8; [[ $zsh_filler_0008 == filler-<-> ]] && print -r -- $zsh_filler_0008 >/dev/null
typeset zsh_filler_0009=filler-9; [[ $zsh_filler_0009 == filler-<-> ]] && print -r -- $zsh_filler_0009 >/dev/null
typeset zsh_filler_0010=filler-10; [[ $zsh_filler_0010 == filler-<-> ]] && print -r -- $zsh_filler_0010 >/dev/null
typeset zsh_filler_0011=filler-11; [[ $zsh_filler_0011 == filler-<-> ]] && print -r -- $zsh_filler_0011 >/dev/null
typeset zsh_filler_0012=filler-12; [[ $zsh_filler_0012 == filler-<-> ]] && print -r -- $zsh_filler_0012 >/dev/null
typeset zsh_filler_0013=filler-13; [[ $zsh_filler_0013 == filler-<-> ]] && print -r -- $zsh_filler_0013 >/dev/null
typeset zsh_filler_0014=filler-14; [[ $zsh_filler_0014 == filler-<-> ]] && print -r -- $zsh_filler_0014 >/dev/null
typeset zsh_filler_0015=filler-15; [[ $zsh_filler_0015 == filler-<-> ]] && print -r -- $zsh_filler_0015 >/dev/null
typeset zsh_filler_0016=filler-16; [[ $zsh_filler_0016 == filler-<-> ]] && print -r -- $zsh_filler_0016 >/dev/null
typeset zsh_filler_0017=filler-17; [[ $zsh_filler_0017 == filler-<-> ]] && print -r -- $zsh_filler_0017 >/dev/null
typeset zsh_filler_0018=filler-18; [[ $zsh_filler_0018 == filler-<-> ]] && print -r -- $zsh_filler_0018 >/dev/null
typeset zsh_filler_0019=filler-19; [[ $zsh_filler_0019 == filler-<-> ]] && print -r -- $zsh_filler_0019 >/dev/null
typeset zsh_filler_0020=filler-20; [[ $zsh_filler_0020 == filler-<-> ]] && print -r -- $zsh_filler_0020 >/dev/null
typeset zsh_filler_0021=filler-21; [[ $zsh_filler_0021 == filler-<-> ]] && print -r -- $zsh_filler_0021 >/dev/null
typeset zsh_filler_0022=filler-22; [[ $zsh_filler_0022 == filler-<-> ]] && print -r -- $zsh_filler_0022 >/dev/null
typeset zsh_filler_0023=filler-23; [[ $zsh_filler_0023 == filler-<-> ]] && print -r -- $zsh_filler_0023 >/dev/null
typeset zsh_filler_0024=filler-24; [[ $zsh_filler_0024 == filler-<-> ]] && print -r -- $zsh_filler_0024 >/dev/null
typeset zsh_filler_0025=filler-25; [[ $zsh_filler_0025 == filler-<-> ]] && print -r -- $zsh_filler_0025 >/dev/null
typeset zsh_filler_0026=filler-26; [[ $zsh_filler_0026 == filler-<-> ]] && print -r -- $zsh_filler_0026 >/dev/null
typeset zsh_filler_0027=filler-27; [[ $zsh_filler_0027 == filler-<-> ]] && print -r -- $zsh_filler_0027 >/dev/null
typeset zsh_filler_0028=filler-28; [[ $zsh_filler_0028 == filler-<-> ]] && print -r -- $zsh_filler_0028 >/dev/null
typeset zsh_filler_0029=filler-29; [[ $zsh_filler_0029 == filler-<-> ]] && print -r -- $zsh_filler_0029 >/dev/null
typeset zsh_filler_0030=filler-30; [[ $zsh_filler_0030 == filler-<-> ]] && print -r -- $zsh_filler_0030 >/dev/null
typeset zsh_filler_0031=filler-31; [[ $zsh_filler_0031 == filler-<-> ]] && print -r -- $zsh_filler_0031 >/dev/null
typeset zsh_filler_0032=filler-32; [[ $zsh_filler_0032 == filler-<-> ]] && print -r -- $zsh_filler_0032 >/dev/null
typeset zsh_filler_0033=filler-33; [[ $zsh_filler_0033 == filler-<-> ]] && print -r -- $zsh_filler_0033 >/dev/null
typeset zsh_filler_0034=filler-34; [[ $zsh_filler_0034 == filler-<-> ]] && print -r -- $zsh_filler_0034 >/dev/null
typeset zsh_filler_0035=filler-35; [[ $zsh_filler_0035 == filler-<-> ]] && print -r -- $zsh_filler_0035 >/dev/null
typeset zsh_filler_0036=filler-36; [[ $zsh_filler_0036 == filler-<-> ]] && print -r -- $zsh_filler_0036 >/dev/null
typeset zsh_filler_0037=filler-37; [[ $zsh_filler_0037 == filler-<-> ]] && print -r -- $zsh_filler_0037 >/dev/null
typeset zsh_filler_0038=filler-38; [[ $zsh_filler_0038 == filler-<-> ]] && print -r -- $zsh_filler_0038 >/dev/null
typeset zsh_filler_0039=filler-39; [[ $zsh_filler_0039 == filler-<-> ]] && print -r -- $zsh_filler_0039 >/dev/null
typeset zsh_filler_0040=filler-40; [[ $zsh_filler_0040 == filler-<-> ]] && print -r -- $zsh_filler_0040 >/dev/null
typeset zsh_filler_0041=filler-41; [[ $zsh_filler_0041 == filler-<-> ]] && print -r -- $zsh_filler_0041 >/dev/null
typeset zsh_filler_0042=filler-42; [[ $zsh_filler_0042 == filler-<-> ]] && print -r -- $zsh_filler_0042 >/dev/null
typeset zsh_filler_0043=filler-43; [[ $zsh_filler_0043 == filler-<-> ]] && print -r -- $zsh_filler_0043 >/dev/null
typeset zsh_filler_0044=filler-44; [[ $zsh_filler_0044 == filler-<-> ]] && print -r -- $zsh_filler_0044 >/dev/null
typeset zsh_filler_0045=filler-45; [[ $zsh_filler_0045 == filler-<-> ]] && print -r -- $zsh_filler_0045 >/dev/null
typeset zsh_filler_0046=filler-46; [[ $zsh_filler_0046 == filler-<-> ]] && print -r -- $zsh_filler_0046 >/dev/null
typeset zsh_filler_0047=filler-47; [[ $zsh_filler_0047 == filler-<-> ]] && print -r -- $zsh_filler_0047 >/dev/null
typeset zsh_filler_0048=filler-48; [[ $zsh_filler_0048 == filler-<-> ]] && print -r -- $zsh_filler_0048 >/dev/null
typeset zsh_filler_0049=filler-49; [[ $zsh_filler_0049 == filler-<-> ]] && print -r -- $zsh_filler_0049 >/dev/null
typeset zsh_filler_0050=filler-50; [[ $zsh_filler_0050 == filler-<-> ]] && print -r -- $zsh_filler_0050 >/dev/null
typeset zsh_filler_0051=filler-51; [[ $zsh_filler_0051 == filler-<-> ]] && print -r -- $zsh_filler_0051 >/dev/null
typeset zsh_filler_0052=filler-52; [[ $zsh_filler_0052 == filler-<-> ]] && print -r -- $zsh_filler_0052 >/dev/null
typeset zsh_filler_0053=filler-53; [[ $zsh_filler_0053 == filler-<-> ]] && print -r -- $zsh_filler_0053 >/dev/null
typeset zsh_filler_0054=filler-54; [[ $zsh_filler_0054 == filler-<-> ]] && print -r -- $zsh_filler_0054 >/dev/null
typeset zsh_filler_0055=filler-55; [[ $zsh_filler_0055 == filler-<-> ]] && print -r -- $zsh_filler_0055 >/dev/null
typeset zsh_filler_0056=filler-56; [[ $zsh_filler_0056 == filler-<-> ]] && print -r -- $zsh_filler_0056 >/dev/null
typeset zsh_filler_0057=filler-57; [[ $zsh_filler_0057 == filler-<-> ]] && print -r -- $zsh_filler_0057 >/dev/null
typeset zsh_filler_0058=filler-58; [[ $zsh_filler_0058 == filler-<-> ]] && print -r -- $zsh_filler_0058 >/dev/null
typeset zsh_filler_0059=filler-59; [[ $zsh_filler_0059 == filler-<-> ]] && print -r -- $zsh_filler_0059 >/dev/null
typeset zsh_filler_0060=filler-60; [[ $zsh_filler_0060 == filler-<-> ]] && print -r -- $zsh_filler_0060 >/dev/null
typeset zsh_filler_0061=filler-61; [[ $zsh_filler_0061 == filler-<-> ]] && print -r -- $zsh_filler_0061 >/dev/null
typeset zsh_filler_0062=filler-62; [[ $zsh_filler_0062 == filler-<-> ]] && print -r -- $zsh_filler_0062 >/dev/null
typeset zsh_filler_0063=filler-63; [[ $zsh_filler_0063 == filler-<-> ]] && print -r -- $zsh_filler_0063 >/dev/null
typeset zsh_filler_0064=filler-64; [[ $zsh_filler_0064 == filler-<-> ]] && print -r -- $zsh_filler_0064 >/dev/null
typeset zsh_filler_0065=filler-65; [[ $zsh_filler_0065 == filler-<-> ]] && print -r -- $zsh_filler_0065 >/dev/null
typeset zsh_filler_0066=filler-66; [[ $zsh_filler_0066 == filler-<-> ]] && print -r -- $zsh_filler_0066 >/dev/null
typeset zsh_filler_0067=filler-67; [[ $zsh_filler_0067 == filler-<-> ]] && print -r -- $zsh_filler_0067 >/dev/null
typeset zsh_filler_0068=filler-68; [[ $zsh_filler_0068 == filler-<-> ]] && print -r -- $zsh_filler_0068 >/dev/null
typeset zsh_filler_0069=filler-69; [[ $zsh_filler_0069 == filler-<-> ]] && print -r -- $zsh_filler_0069 >/dev/null
typeset zsh_filler_0070=filler-70; [[ $zsh_filler_0070 == filler-<-> ]] && print -r -- $zsh_filler_0070 >/dev/null
typeset zsh_filler_0071=filler-71; [[ $zsh_filler_0071 == filler-<-> ]] && print -r -- $zsh_filler_0071 >/dev/null
typeset zsh_filler_0072=filler-72; [[ $zsh_filler_0072 == filler-<-> ]] && print -r -- $zsh_filler_0072 >/dev/null
typeset zsh_filler_0073=filler-73; [[ $zsh_filler_0073 == filler-<-> ]] && print -r -- $zsh_filler_0073 >/dev/null
typeset zsh_filler_0074=filler-74; [[ $zsh_filler_0074 == filler-<-> ]] && print -r -- $zsh_filler_0074 >/dev/null
typeset zsh_filler_0075=filler-75; [[ $zsh_filler_0075 == filler-<-> ]] && print -r -- $zsh_filler_0075 >/dev/null
typeset zsh_filler_0076=filler-76; [[ $zsh_filler_0076 == filler-<-> ]] && print -r -- $zsh_filler_0076 >/dev/null
typeset zsh_filler_0077=filler-77; [[ $zsh_filler_0077 == filler-<-> ]] && print -r -- $zsh_filler_0077 >/dev/null
typeset zsh_filler_0078=filler-78; [[ $zsh_filler_0078 == filler-<-> ]] && print -r -- $zsh_filler_0078 >/dev/null
typeset zsh_filler_0079=filler-79; [[ $zsh_filler_0079 == filler-<-> ]] && print -r -- $zsh_filler_0079 >/dev/null
typeset zsh_filler_0080=filler-80; [[ $zsh_filler_0080 == filler-<-> ]] && print -r -- $zsh_filler_0080 >/dev/null
typeset zsh_filler_0081=filler-81; [[ $zsh_filler_0081 == filler-<-> ]] && print -r -- $zsh_filler_0081 >/dev/null
typeset zsh_filler_0082=filler-82; [[ $zsh_filler_0082 == filler-<-> ]] && print -r -- $zsh_filler_0082 >/dev/null
typeset zsh_filler_0083=filler-83; [[ $zsh_filler_0083 == filler-<-> ]] && print -r -- $zsh_filler_0083 >/dev/null
typeset zsh_filler_0084=filler-84; [[ $zsh_filler_0084 == filler-<-> ]] && print -r -- $zsh_filler_0084 >/dev/null
typeset zsh_filler_0085=filler-85; [[ $zsh_filler_0085 == filler-<-> ]] && print -r -- $zsh_filler_0085 >/dev/null
typeset zsh_filler_0086=filler-86; [[ $zsh_filler_0086 == filler-<-> ]] && print -r -- $zsh_filler_0086 >/dev/null
typeset zsh_filler_0087=filler-87; [[ $zsh_filler_0087 == filler-<-> ]] && print -r -- $zsh_filler_0087 >/dev/null
typeset zsh_filler_0088=filler-88; [[ $zsh_filler_0088 == filler-<-> ]] && print -r -- $zsh_filler_0088 >/dev/null
typeset zsh_filler_0089=filler-89; [[ $zsh_filler_0089 == filler-<-> ]] && print -r -- $zsh_filler_0089 >/dev/null
typeset zsh_filler_0090=filler-90; [[ $zsh_filler_0090 == filler-<-> ]] && print -r -- $zsh_filler_0090 >/dev/null
typeset zsh_filler_0091=filler-91; [[ $zsh_filler_0091 == filler-<-> ]] && print -r -- $zsh_filler_0091 >/dev/null
typeset zsh_filler_0092=filler-92; [[ $zsh_filler_0092 == filler-<-> ]] && print -r -- $zsh_filler_0092 >/dev/null
typeset zsh_filler_0093=filler-93; [[ $zsh_filler_0093 == filler-<-> ]] && print -r -- $zsh_filler_0093 >/dev/null
typeset zsh_filler_0094=filler-94; [[ $zsh_filler_0094 == filler-<-> ]] && print -r -- $zsh_filler_0094 >/dev/null
typeset zsh_filler_0095=filler-95; [[ $zsh_filler_0095 == filler-<-> ]] && print -r -- $zsh_filler_0095 >/dev/null
typeset zsh_filler_0096=filler-96; [[ $zsh_filler_0096 == filler-<-> ]] && print -r -- $zsh_filler_0096 >/dev/null
typeset zsh_filler_0097=filler-97; [[ $zsh_filler_0097 == filler-<-> ]] && print -r -- $zsh_filler_0097 >/dev/null
typeset zsh_filler_0098=filler-98; [[ $zsh_filler_0098 == filler-<-> ]] && print -r -- $zsh_filler_0098 >/dev/null
typeset zsh_filler_0099=filler-99; [[ $zsh_filler_0099 == filler-<-> ]] && print -r -- $zsh_filler_0099 >/dev/null
typeset zsh_filler_0100=filler-100; [[ $zsh_filler_0100 == filler-<-> ]] && print -r -- $zsh_filler_0100 >/dev/null
typeset zsh_filler_0101=filler-101; [[ $zsh_filler_0101 == filler-<-> ]] && print -r -- $zsh_filler_0101 >/dev/null
typeset zsh_filler_0102=filler-102; [[ $zsh_filler_0102 == filler-<-> ]] && print -r -- $zsh_filler_0102 >/dev/null
typeset zsh_filler_0103=filler-103; [[ $zsh_filler_0103 == filler-<-> ]] && print -r -- $zsh_filler_0103 >/dev/null
typeset zsh_filler_0104=filler-104; [[ $zsh_filler_0104 == filler-<-> ]] && print -r -- $zsh_filler_0104 >/dev/null
typeset zsh_filler_0105=filler-105; [[ $zsh_filler_0105 == filler-<-> ]] && print -r -- $zsh_filler_0105 >/dev/null
typeset zsh_filler_0106=filler-106; [[ $zsh_filler_0106 == filler-<-> ]] && print -r -- $zsh_filler_0106 >/dev/null
typeset zsh_filler_0107=filler-107; [[ $zsh_filler_0107 == filler-<-> ]] && print -r -- $zsh_filler_0107 >/dev/null
typeset zsh_filler_0108=filler-108; [[ $zsh_filler_0108 == filler-<-> ]] && print -r -- $zsh_filler_0108 >/dev/null
typeset zsh_filler_0109=filler-109; [[ $zsh_filler_0109 == filler-<-> ]] && print -r -- $zsh_filler_0109 >/dev/null
typeset zsh_filler_0110=filler-110; [[ $zsh_filler_0110 == filler-<-> ]] && print -r -- $zsh_filler_0110 >/dev/null
typeset zsh_filler_0111=filler-111; [[ $zsh_filler_0111 == filler-<-> ]] && print -r -- $zsh_filler_0111 >/dev/null
typeset zsh_filler_0112=filler-112; [[ $zsh_filler_0112 == filler-<-> ]] && print -r -- $zsh_filler_0112 >/dev/null
typeset zsh_filler_0113=filler-113; [[ $zsh_filler_0113 == filler-<-> ]] && print -r -- $zsh_filler_0113 >/dev/null
typeset zsh_filler_0114=filler-114; [[ $zsh_filler_0114 == filler-<-> ]] && print -r -- $zsh_filler_0114 >/dev/null
typeset zsh_filler_0115=filler-115; [[ $zsh_filler_0115 == filler-<-> ]] && print -r -- $zsh_filler_0115 >/dev/null
typeset zsh_filler_0116=filler-116; [[ $zsh_filler_0116 == filler-<-> ]] && print -r -- $zsh_filler_0116 >/dev/null
typeset zsh_filler_0117=filler-117; [[ $zsh_filler_0117 == filler-<-> ]] && print -r -- $zsh_filler_0117 >/dev/null
typeset zsh_filler_0118=filler-118; [[ $zsh_filler_0118 == filler-<-> ]] && print -r -- $zsh_filler_0118 >/dev/null
typeset zsh_filler_0119=filler-119; [[ $zsh_filler_0119 == filler-<-> ]] && print -r -- $zsh_filler_0119 >/dev/null
typeset zsh_filler_0120=filler-120; [[ $zsh_filler_0120 == filler-<-> ]] && print -r -- $zsh_filler_0120 >/dev/null
typeset zsh_filler_0121=filler-121; [[ $zsh_filler_0121 == filler-<-> ]] && print -r -- $zsh_filler_0121 >/dev/null
typeset zsh_filler_0122=filler-122; [[ $zsh_filler_0122 == filler-<-> ]] && print -r -- $zsh_filler_0122 >/dev/null
typeset zsh_filler_0123=filler-123; [[ $zsh_filler_0123 == filler-<-> ]] && print -r -- $zsh_filler_0123 >/dev/null
typeset zsh_filler_0124=filler-124; [[ $zsh_filler_0124 == filler-<-> ]] && print -r -- $zsh_filler_0124 >/dev/null
typeset zsh_filler_0125=filler-125; [[ $zsh_filler_0125 == filler-<-> ]] && print -r -- $zsh_filler_0125 >/dev/null
typeset zsh_filler_0126=filler-126; [[ $zsh_filler_0126 == filler-<-> ]] && print -r -- $zsh_filler_0126 >/dev/null
typeset zsh_filler_0127=filler-127; [[ $zsh_filler_0127 == filler-<-> ]] && print -r -- $zsh_filler_0127 >/dev/null
typeset zsh_filler_0128=filler-128; [[ $zsh_filler_0128 == filler-<-> ]] && print -r -- $zsh_filler_0128 >/dev/null
typeset zsh_filler_0129=filler-129; [[ $zsh_filler_0129 == filler-<-> ]] && print -r -- $zsh_filler_0129 >/dev/null
typeset zsh_filler_0130=filler-130; [[ $zsh_filler_0130 == filler-<-> ]] && print -r -- $zsh_filler_0130 >/dev/null
typeset zsh_filler_0131=filler-131; [[ $zsh_filler_0131 == filler-<-> ]] && print -r -- $zsh_filler_0131 >/dev/null
typeset zsh_filler_0132=filler-132; [[ $zsh_filler_0132 == filler-<-> ]] && print -r -- $zsh_filler_0132 >/dev/null
typeset zsh_filler_0133=filler-133; [[ $zsh_filler_0133 == filler-<-> ]] && print -r -- $zsh_filler_0133 >/dev/null
typeset zsh_filler_0134=filler-134; [[ $zsh_filler_0134 == filler-<-> ]] && print -r -- $zsh_filler_0134 >/dev/null
typeset zsh_filler_0135=filler-135; [[ $zsh_filler_0135 == filler-<-> ]] && print -r -- $zsh_filler_0135 >/dev/null
typeset zsh_filler_0136=filler-136; [[ $zsh_filler_0136 == filler-<-> ]] && print -r -- $zsh_filler_0136 >/dev/null
typeset zsh_filler_0137=filler-137; [[ $zsh_filler_0137 == filler-<-> ]] && print -r -- $zsh_filler_0137 >/dev/null
typeset zsh_filler_0138=filler-138; [[ $zsh_filler_0138 == filler-<-> ]] && print -r -- $zsh_filler_0138 >/dev/null
typeset zsh_filler_0139=filler-139; [[ $zsh_filler_0139 == filler-<-> ]] && print -r -- $zsh_filler_0139 >/dev/null
typeset zsh_filler_0140=filler-140; [[ $zsh_filler_0140 == filler-<-> ]] && print -r -- $zsh_filler_0140 >/dev/null
typeset zsh_filler_0141=filler-141; [[ $zsh_filler_0141 == filler-<-> ]] && print -r -- $zsh_filler_0141 >/dev/null
typeset zsh_filler_0142=filler-142; [[ $zsh_filler_0142 == filler-<-> ]] && print -r -- $zsh_filler_0142 >/dev/null
typeset zsh_filler_0143=filler-143; [[ $zsh_filler_0143 == filler-<-> ]] && print -r -- $zsh_filler_0143 >/dev/null
typeset zsh_filler_0144=filler-144; [[ $zsh_filler_0144 == filler-<-> ]] && print -r -- $zsh_filler_0144 >/dev/null
typeset zsh_filler_0145=filler-145; [[ $zsh_filler_0145 == filler-<-> ]] && print -r -- $zsh_filler_0145 >/dev/null
typeset zsh_filler_0146=filler-146; [[ $zsh_filler_0146 == filler-<-> ]] && print -r -- $zsh_filler_0146 >/dev/null
typeset zsh_filler_0147=filler-147; [[ $zsh_filler_0147 == filler-<-> ]] && print -r -- $zsh_filler_0147 >/dev/null
typeset zsh_filler_0148=filler-148; [[ $zsh_filler_0148 == filler-<-> ]] && print -r -- $zsh_filler_0148 >/dev/null
typeset zsh_filler_0149=filler-149; [[ $zsh_filler_0149 == filler-<-> ]] && print -r -- $zsh_filler_0149 >/dev/null
typeset zsh_filler_0150=filler-150; [[ $zsh_filler_0150 == filler-<-> ]] && print -r -- $zsh_filler_0150 >/dev/null
typeset zsh_filler_0151=filler-151; [[ $zsh_filler_0151 == filler-<-> ]] && print -r -- $zsh_filler_0151 >/dev/null
typeset zsh_filler_0152=filler-152; [[ $zsh_filler_0152 == filler-<-> ]] && print -r -- $zsh_filler_0152 >/dev/null
typeset zsh_filler_0153=filler-153; [[ $zsh_filler_0153 == filler-<-> ]] && print -r -- $zsh_filler_0153 >/dev/null
typeset zsh_filler_0154=filler-154; [[ $zsh_filler_0154 == filler-<-> ]] && print -r -- $zsh_filler_0154 >/dev/null
typeset zsh_filler_0155=filler-155; [[ $zsh_filler_0155 == filler-<-> ]] && print -r -- $zsh_filler_0155 >/dev/null
typeset zsh_filler_0156=filler-156; [[ $zsh_filler_0156 == filler-<-> ]] && print -r -- $zsh_filler_0156 >/dev/null
typeset zsh_filler_0157=filler-157; [[ $zsh_filler_0157 == filler-<-> ]] && print -r -- $zsh_filler_0157 >/dev/null
typeset zsh_filler_0158=filler-158; [[ $zsh_filler_0158 == filler-<-> ]] && print -r -- $zsh_filler_0158 >/dev/null
typeset zsh_filler_0159=filler-159; [[ $zsh_filler_0159 == filler-<-> ]] && print -r -- $zsh_filler_0159 >/dev/null
typeset zsh_filler_0160=filler-160; [[ $zsh_filler_0160 == filler-<-> ]] && print -r -- $zsh_filler_0160 >/dev/null
typeset zsh_filler_0161=filler-161; [[ $zsh_filler_0161 == filler-<-> ]] && print -r -- $zsh_filler_0161 >/dev/null
typeset zsh_filler_0162=filler-162; [[ $zsh_filler_0162 == filler-<-> ]] && print -r -- $zsh_filler_0162 >/dev/null
typeset zsh_filler_0163=filler-163; [[ $zsh_filler_0163 == filler-<-> ]] && print -r -- $zsh_filler_0163 >/dev/null
typeset zsh_filler_0164=filler-164; [[ $zsh_filler_0164 == filler-<-> ]] && print -r -- $zsh_filler_0164 >/dev/null
typeset zsh_filler_0165=filler-165; [[ $zsh_filler_0165 == filler-<-> ]] && print -r -- $zsh_filler_0165 >/dev/null
typeset zsh_filler_0166=filler-166; [[ $zsh_filler_0166 == filler-<-> ]] && print -r -- $zsh_filler_0166 >/dev/null
typeset zsh_filler_0167=filler-167; [[ $zsh_filler_0167 == filler-<-> ]] && print -r -- $zsh_filler_0167 >/dev/null
typeset zsh_filler_0168=filler-168; [[ $zsh_filler_0168 == filler-<-> ]] && print -r -- $zsh_filler_0168 >/dev/null
typeset zsh_filler_0169=filler-169; [[ $zsh_filler_0169 == filler-<-> ]] && print -r -- $zsh_filler_0169 >/dev/null
typeset zsh_filler_0170=filler-170; [[ $zsh_filler_0170 == filler-<-> ]] && print -r -- $zsh_filler_0170 >/dev/null
typeset zsh_filler_0171=filler-171; [[ $zsh_filler_0171 == filler-<-> ]] && print -r -- $zsh_filler_0171 >/dev/null
typeset zsh_filler_0172=filler-172; [[ $zsh_filler_0172 == filler-<-> ]] && print -r -- $zsh_filler_0172 >/dev/null
typeset zsh_filler_0173=filler-173; [[ $zsh_filler_0173 == filler-<-> ]] && print -r -- $zsh_filler_0173 >/dev/null
typeset zsh_filler_0174=filler-174; [[ $zsh_filler_0174 == filler-<-> ]] && print -r -- $zsh_filler_0174 >/dev/null
typeset zsh_filler_0175=filler-175; [[ $zsh_filler_0175 == filler-<-> ]] && print -r -- $zsh_filler_0175 >/dev/null
typeset zsh_filler_0176=filler-176; [[ $zsh_filler_0176 == filler-<-> ]] && print -r -- $zsh_filler_0176 >/dev/null
typeset zsh_filler_0177=filler-177; [[ $zsh_filler_0177 == filler-<-> ]] && print -r -- $zsh_filler_0177 >/dev/null
typeset zsh_filler_0178=filler-178; [[ $zsh_filler_0178 == filler-<-> ]] && print -r -- $zsh_filler_0178 >/dev/null
typeset zsh_filler_0179=filler-179; [[ $zsh_filler_0179 == filler-<-> ]] && print -r -- $zsh_filler_0179 >/dev/null
typeset zsh_filler_0180=filler-180; [[ $zsh_filler_0180 == filler-<-> ]] && print -r -- $zsh_filler_0180 >/dev/null
typeset zsh_filler_0181=filler-181; [[ $zsh_filler_0181 == filler-<-> ]] && print -r -- $zsh_filler_0181 >/dev/null
typeset zsh_filler_0182=filler-182; [[ $zsh_filler_0182 == filler-<-> ]] && print -r -- $zsh_filler_0182 >/dev/null
typeset zsh_filler_0183=filler-183; [[ $zsh_filler_0183 == filler-<-> ]] && print -r -- $zsh_filler_0183 >/dev/null
typeset zsh_filler_0184=filler-184; [[ $zsh_filler_0184 == filler-<-> ]] && print -r -- $zsh_filler_0184 >/dev/null
typeset zsh_filler_0185=filler-185; [[ $zsh_filler_0185 == filler-<-> ]] && print -r -- $zsh_filler_0185 >/dev/null
typeset zsh_filler_0186=filler-186; [[ $zsh_filler_0186 == filler-<-> ]] && print -r -- $zsh_filler_0186 >/dev/null
typeset zsh_filler_0187=filler-187; [[ $zsh_filler_0187 == filler-<-> ]] && print -r -- $zsh_filler_0187 >/dev/null
typeset zsh_filler_0188=filler-188; [[ $zsh_filler_0188 == filler-<-> ]] && print -r -- $zsh_filler_0188 >/dev/null
typeset zsh_filler_0189=filler-189; [[ $zsh_filler_0189 == filler-<-> ]] && print -r -- $zsh_filler_0189 >/dev/null
typeset zsh_filler_0190=filler-190; [[ $zsh_filler_0190 == filler-<-> ]] && print -r -- $zsh_filler_0190 >/dev/null
typeset zsh_filler_0191=filler-191; [[ $zsh_filler_0191 == filler-<-> ]] && print -r -- $zsh_filler_0191 >/dev/null
typeset zsh_filler_0192=filler-192; [[ $zsh_filler_0192 == filler-<-> ]] && print -r -- $zsh_filler_0192 >/dev/null
typeset zsh_filler_0193=filler-193; [[ $zsh_filler_0193 == filler-<-> ]] && print -r -- $zsh_filler_0193 >/dev/null
typeset zsh_filler_0194=filler-194; [[ $zsh_filler_0194 == filler-<-> ]] && print -r -- $zsh_filler_0194 >/dev/null
typeset zsh_filler_0195=filler-195; [[ $zsh_filler_0195 == filler-<-> ]] && print -r -- $zsh_filler_0195 >/dev/null
typeset zsh_filler_0196=filler-196; [[ $zsh_filler_0196 == filler-<-> ]] && print -r -- $zsh_filler_0196 >/dev/null
typeset zsh_filler_0197=filler-197; [[ $zsh_filler_0197 == filler-<-> ]] && print -r -- $zsh_filler_0197 >/dev/null
typeset zsh_filler_0198=filler-198; [[ $zsh_filler_0198 == filler-<-> ]] && print -r -- $zsh_filler_0198 >/dev/null
typeset zsh_filler_0199=filler-199; [[ $zsh_filler_0199 == filler-<-> ]] && print -r -- $zsh_filler_0199 >/dev/null
typeset zsh_filler_0200=filler-200; [[ $zsh_filler_0200 == filler-<-> ]] && print -r -- $zsh_filler_0200 >/dev/null
typeset zsh_filler_0201=filler-201; [[ $zsh_filler_0201 == filler-<-> ]] && print -r -- $zsh_filler_0201 >/dev/null
typeset zsh_filler_0202=filler-202; [[ $zsh_filler_0202 == filler-<-> ]] && print -r -- $zsh_filler_0202 >/dev/null
typeset zsh_filler_0203=filler-203; [[ $zsh_filler_0203 == filler-<-> ]] && print -r -- $zsh_filler_0203 >/dev/null
typeset zsh_filler_0204=filler-204; [[ $zsh_filler_0204 == filler-<-> ]] && print -r -- $zsh_filler_0204 >/dev/null
typeset zsh_filler_0205=filler-205; [[ $zsh_filler_0205 == filler-<-> ]] && print -r -- $zsh_filler_0205 >/dev/null
typeset zsh_filler_0206=filler-206; [[ $zsh_filler_0206 == filler-<-> ]] && print -r -- $zsh_filler_0206 >/dev/null
typeset zsh_filler_0207=filler-207; [[ $zsh_filler_0207 == filler-<-> ]] && print -r -- $zsh_filler_0207 >/dev/null
typeset zsh_filler_0208=filler-208; [[ $zsh_filler_0208 == filler-<-> ]] && print -r -- $zsh_filler_0208 >/dev/null
typeset zsh_filler_0209=filler-209; [[ $zsh_filler_0209 == filler-<-> ]] && print -r -- $zsh_filler_0209 >/dev/null
typeset zsh_filler_0210=filler-210; [[ $zsh_filler_0210 == filler-<-> ]] && print -r -- $zsh_filler_0210 >/dev/null
typeset zsh_filler_0211=filler-211; [[ $zsh_filler_0211 == filler-<-> ]] && print -r -- $zsh_filler_0211 >/dev/null
typeset zsh_filler_0212=filler-212; [[ $zsh_filler_0212 == filler-<-> ]] && print -r -- $zsh_filler_0212 >/dev/null
typeset zsh_filler_0213=filler-213; [[ $zsh_filler_0213 == filler-<-> ]] && print -r -- $zsh_filler_0213 >/dev/null
typeset zsh_filler_0214=filler-214; [[ $zsh_filler_0214 == filler-<-> ]] && print -r -- $zsh_filler_0214 >/dev/null
typeset zsh_filler_0215=filler-215; [[ $zsh_filler_0215 == filler-<-> ]] && print -r -- $zsh_filler_0215 >/dev/null
typeset zsh_filler_0216=filler-216; [[ $zsh_filler_0216 == filler-<-> ]] && print -r -- $zsh_filler_0216 >/dev/null
typeset zsh_filler_0217=filler-217; [[ $zsh_filler_0217 == filler-<-> ]] && print -r -- $zsh_filler_0217 >/dev/null
typeset zsh_filler_0218=filler-218; [[ $zsh_filler_0218 == filler-<-> ]] && print -r -- $zsh_filler_0218 >/dev/null
typeset zsh_filler_0219=filler-219; [[ $zsh_filler_0219 == filler-<-> ]] && print -r -- $zsh_filler_0219 >/dev/null
typeset zsh_filler_0220=filler-220; [[ $zsh_filler_0220 == filler-<-> ]] && print -r -- $zsh_filler_0220 >/dev/null
typeset zsh_filler_0221=filler-221; [[ $zsh_filler_0221 == filler-<-> ]] && print -r -- $zsh_filler_0221 >/dev/null
typeset zsh_filler_0222=filler-222; [[ $zsh_filler_0222 == filler-<-> ]] && print -r -- $zsh_filler_0222 >/dev/null
typeset zsh_filler_0223=filler-223; [[ $zsh_filler_0223 == filler-<-> ]] && print -r -- $zsh_filler_0223 >/dev/null
typeset zsh_filler_0224=filler-224; [[ $zsh_filler_0224 == filler-<-> ]] && print -r -- $zsh_filler_0224 >/dev/null
typeset zsh_filler_0225=filler-225; [[ $zsh_filler_0225 == filler-<-> ]] && print -r -- $zsh_filler_0225 >/dev/null
typeset zsh_filler_0226=filler-226; [[ $zsh_filler_0226 == filler-<-> ]] && print -r -- $zsh_filler_0226 >/dev/null
typeset zsh_filler_0227=filler-227; [[ $zsh_filler_0227 == filler-<-> ]] && print -r -- $zsh_filler_0227 >/dev/null
typeset zsh_filler_0228=filler-228; [[ $zsh_filler_0228 == filler-<-> ]] && print -r -- $zsh_filler_0228 >/dev/null
typeset zsh_filler_0229=filler-229; [[ $zsh_filler_0229 == filler-<-> ]] && print -r -- $zsh_filler_0229 >/dev/null
typeset zsh_filler_0230=filler-230; [[ $zsh_filler_0230 == filler-<-> ]] && print -r -- $zsh_filler_0230 >/dev/null
typeset zsh_filler_0231=filler-231; [[ $zsh_filler_0231 == filler-<-> ]] && print -r -- $zsh_filler_0231 >/dev/null
typeset zsh_filler_0232=filler-232; [[ $zsh_filler_0232 == filler-<-> ]] && print -r -- $zsh_filler_0232 >/dev/null
typeset zsh_filler_0233=filler-233; [[ $zsh_filler_0233 == filler-<-> ]] && print -r -- $zsh_filler_0233 >/dev/null
typeset zsh_filler_0234=filler-234; [[ $zsh_filler_0234 == filler-<-> ]] && print -r -- $zsh_filler_0234 >/dev/null
typeset zsh_filler_0235=filler-235; [[ $zsh_filler_0235 == filler-<-> ]] && print -r -- $zsh_filler_0235 >/dev/null
typeset zsh_filler_0236=filler-236; [[ $zsh_filler_0236 == filler-<-> ]] && print -r -- $zsh_filler_0236 >/dev/null
typeset zsh_filler_0237=filler-237; [[ $zsh_filler_0237 == filler-<-> ]] && print -r -- $zsh_filler_0237 >/dev/null
typeset zsh_filler_0238=filler-238; [[ $zsh_filler_0238 == filler-<-> ]] && print -r -- $zsh_filler_0238 >/dev/null
typeset zsh_filler_0239=filler-239; [[ $zsh_filler_0239 == filler-<-> ]] && print -r -- $zsh_filler_0239 >/dev/null
typeset zsh_filler_0240=filler-240; [[ $zsh_filler_0240 == filler-<-> ]] && print -r -- $zsh_filler_0240 >/dev/null
typeset zsh_filler_0241=filler-241; [[ $zsh_filler_0241 == filler-<-> ]] && print -r -- $zsh_filler_0241 >/dev/null
typeset zsh_filler_0242=filler-242; [[ $zsh_filler_0242 == filler-<-> ]] && print -r -- $zsh_filler_0242 >/dev/null
typeset zsh_filler_0243=filler-243; [[ $zsh_filler_0243 == filler-<-> ]] && print -r -- $zsh_filler_0243 >/dev/null
typeset zsh_filler_0244=filler-244; [[ $zsh_filler_0244 == filler-<-> ]] && print -r -- $zsh_filler_0244 >/dev/null
typeset zsh_filler_0245=filler-245; [[ $zsh_filler_0245 == filler-<-> ]] && print -r -- $zsh_filler_0245 >/dev/null
typeset zsh_filler_0246=filler-246; [[ $zsh_filler_0246 == filler-<-> ]] && print -r -- $zsh_filler_0246 >/dev/null
typeset zsh_filler_0247=filler-247; [[ $zsh_filler_0247 == filler-<-> ]] && print -r -- $zsh_filler_0247 >/dev/null
typeset zsh_filler_0248=filler-248; [[ $zsh_filler_0248 == filler-<-> ]] && print -r -- $zsh_filler_0248 >/dev/null
typeset zsh_filler_0249=filler-249; [[ $zsh_filler_0249 == filler-<-> ]] && print -r -- $zsh_filler_0249 >/dev/null
typeset zsh_filler_0250=filler-250; [[ $zsh_filler_0250 == filler-<-> ]] && print -r -- $zsh_filler_0250 >/dev/null
typeset zsh_filler_0251=filler-251; [[ $zsh_filler_0251 == filler-<-> ]] && print -r -- $zsh_filler_0251 >/dev/null
typeset zsh_filler_0252=filler-252; [[ $zsh_filler_0252 == filler-<-> ]] && print -r -- $zsh_filler_0252 >/dev/null
typeset zsh_filler_0253=filler-253; [[ $zsh_filler_0253 == filler-<-> ]] && print -r -- $zsh_filler_0253 >/dev/null
typeset zsh_filler_0254=filler-254; [[ $zsh_filler_0254 == filler-<-> ]] && print -r -- $zsh_filler_0254 >/dev/null
typeset zsh_filler_0255=filler-255; [[ $zsh_filler_0255 == filler-<-> ]] && print -r -- $zsh_filler_0255 >/dev/null
typeset zsh_filler_0256=filler-256; [[ $zsh_filler_0256 == filler-<-> ]] && print -r -- $zsh_filler_0256 >/dev/null
typeset zsh_filler_0257=filler-257; [[ $zsh_filler_0257 == filler-<-> ]] && print -r -- $zsh_filler_0257 >/dev/null
typeset zsh_filler_0258=filler-258; [[ $zsh_filler_0258 == filler-<-> ]] && print -r -- $zsh_filler_0258 >/dev/null
typeset zsh_filler_0259=filler-259; [[ $zsh_filler_0259 == filler-<-> ]] && print -r -- $zsh_filler_0259 >/dev/null
typeset zsh_filler_0260=filler-260; [[ $zsh_filler_0260 == filler-<-> ]] && print -r -- $zsh_filler_0260 >/dev/null
typeset zsh_filler_0261=filler-261; [[ $zsh_filler_0261 == filler-<-> ]] && print -r -- $zsh_filler_0261 >/dev/null
typeset zsh_filler_0262=filler-262; [[ $zsh_filler_0262 == filler-<-> ]] && print -r -- $zsh_filler_0262 >/dev/null
typeset zsh_filler_0263=filler-263; [[ $zsh_filler_0263 == filler-<-> ]] && print -r -- $zsh_filler_0263 >/dev/null
typeset zsh_filler_0264=filler-264; [[ $zsh_filler_0264 == filler-<-> ]] && print -r -- $zsh_filler_0264 >/dev/null
typeset zsh_filler_0265=filler-265; [[ $zsh_filler_0265 == filler-<-> ]] && print -r -- $zsh_filler_0265 >/dev/null
typeset zsh_filler_0266=filler-266; [[ $zsh_filler_0266 == filler-<-> ]] && print -r -- $zsh_filler_0266 >/dev/null
typeset zsh_filler_0267=filler-267; [[ $zsh_filler_0267 == filler-<-> ]] && print -r -- $zsh_filler_0267 >/dev/null
typeset zsh_filler_0268=filler-268; [[ $zsh_filler_0268 == filler-<-> ]] && print -r -- $zsh_filler_0268 >/dev/null
typeset zsh_filler_0269=filler-269; [[ $zsh_filler_0269 == filler-<-> ]] && print -r -- $zsh_filler_0269 >/dev/null
typeset zsh_filler_0270=filler-270; [[ $zsh_filler_0270 == filler-<-> ]] && print -r -- $zsh_filler_0270 >/dev/null
typeset zsh_filler_0271=filler-271; [[ $zsh_filler_0271 == filler-<-> ]] && print -r -- $zsh_filler_0271 >/dev/null
typeset zsh_filler_0272=filler-272; [[ $zsh_filler_0272 == filler-<-> ]] && print -r -- $zsh_filler_0272 >/dev/null
typeset zsh_filler_0273=filler-273; [[ $zsh_filler_0273 == filler-<-> ]] && print -r -- $zsh_filler_0273 >/dev/null
typeset zsh_filler_0274=filler-274; [[ $zsh_filler_0274 == filler-<-> ]] && print -r -- $zsh_filler_0274 >/dev/null
typeset zsh_filler_0275=filler-275; [[ $zsh_filler_0275 == filler-<-> ]] && print -r -- $zsh_filler_0275 >/dev/null
typeset zsh_filler_0276=filler-276; [[ $zsh_filler_0276 == filler-<-> ]] && print -r -- $zsh_filler_0276 >/dev/null
typeset zsh_filler_0277=filler-277; [[ $zsh_filler_0277 == filler-<-> ]] && print -r -- $zsh_filler_0277 >/dev/null
typeset zsh_filler_0278=filler-278; [[ $zsh_filler_0278 == filler-<-> ]] && print -r -- $zsh_filler_0278 >/dev/null
typeset zsh_filler_0279=filler-279; [[ $zsh_filler_0279 == filler-<-> ]] && print -r -- $zsh_filler_0279 >/dev/null
typeset zsh_filler_0280=filler-280; [[ $zsh_filler_0280 == filler-<-> ]] && print -r -- $zsh_filler_0280 >/dev/null
typeset zsh_filler_0281=filler-281; [[ $zsh_filler_0281 == filler-<-> ]] && print -r -- $zsh_filler_0281 >/dev/null
typeset zsh_filler_0282=filler-282; [[ $zsh_filler_0282 == filler-<-> ]] && print -r -- $zsh_filler_0282 >/dev/null
typeset zsh_filler_0283=filler-283; [[ $zsh_filler_0283 == filler-<-> ]] && print -r -- $zsh_filler_0283 >/dev/null
typeset zsh_filler_0284=filler-284; [[ $zsh_filler_0284 == filler-<-> ]] && print -r -- $zsh_filler_0284 >/dev/null
typeset zsh_filler_0285=filler-285; [[ $zsh_filler_0285 == filler-<-> ]] && print -r -- $zsh_filler_0285 >/dev/null
typeset zsh_filler_0286=filler-286; [[ $zsh_filler_0286 == filler-<-> ]] && print -r -- $zsh_filler_0286 >/dev/null
typeset zsh_filler_0287=filler-287; [[ $zsh_filler_0287 == filler-<-> ]] && print -r -- $zsh_filler_0287 >/dev/null
typeset zsh_filler_0288=filler-288; [[ $zsh_filler_0288 == filler-<-> ]] && print -r -- $zsh_filler_0288 >/dev/null
typeset zsh_filler_0289=filler-289; [[ $zsh_filler_0289 == filler-<-> ]] && print -r -- $zsh_filler_0289 >/dev/null
typeset zsh_filler_0290=filler-290; [[ $zsh_filler_0290 == filler-<-> ]] && print -r -- $zsh_filler_0290 >/dev/null
typeset zsh_filler_0291=filler-291; [[ $zsh_filler_0291 == filler-<-> ]] && print -r -- $zsh_filler_0291 >/dev/null
typeset zsh_filler_0292=filler-292; [[ $zsh_filler_0292 == filler-<-> ]] && print -r -- $zsh_filler_0292 >/dev/null
typeset zsh_filler_0293=filler-293; [[ $zsh_filler_0293 == filler-<-> ]] && print -r -- $zsh_filler_0293 >/dev/null
typeset zsh_filler_0294=filler-294; [[ $zsh_filler_0294 == filler-<-> ]] && print -r -- $zsh_filler_0294 >/dev/null
typeset zsh_filler_0295=filler-295; [[ $zsh_filler_0295 == filler-<-> ]] && print -r -- $zsh_filler_0295 >/dev/null
typeset zsh_filler_0296=filler-296; [[ $zsh_filler_0296 == filler-<-> ]] && print -r -- $zsh_filler_0296 >/dev/null
typeset zsh_filler_0297=filler-297; [[ $zsh_filler_0297 == filler-<-> ]] && print -r -- $zsh_filler_0297 >/dev/null
typeset zsh_filler_0298=filler-298; [[ $zsh_filler_0298 == filler-<-> ]] && print -r -- $zsh_filler_0298 >/dev/null
typeset zsh_filler_0299=filler-299; [[ $zsh_filler_0299 == filler-<-> ]] && print -r -- $zsh_filler_0299 >/dev/null
typeset zsh_filler_0300=filler-300; [[ $zsh_filler_0300 == filler-<-> ]] && print -r -- $zsh_filler_0300 >/dev/null
typeset zsh_filler_0301=filler-301; [[ $zsh_filler_0301 == filler-<-> ]] && print -r -- $zsh_filler_0301 >/dev/null
main() { arithmetic_examples; conditional_examples ${1:-alpha}; case_examples "$@"; loop_examples; flags_examples; glob_examples; redirection_examples; zparseopts_example "$@"; heredoc_examples; style_examples; }
main "$@"
