#!/usr/bin/env perl
use v5.36;
use strict;
use warnings;
use utf8;
use feature qw(say state signatures current_sub fc postderef try);
no warnings qw(experimental::signatures experimental::postderef experimental::try experimental::smartmatch);
use integer;
use bytes;
use charnames ':full';
use constant PI => 3.141592653589793;
use constant DEBUG => !!1;
use overload '""' => 'as_string', '0+' => 'as_number', fallback => 1;
use vars qw($GLOBAL_SCALAR @GLOBAL_ARRAY %GLOBAL_HASH *GLOBAL_GLOB);
our $VERSION = '0.001_lexer';
our $AUTOLOAD;
local $SIG{__WARN__} = sub { CORE::warn "WARN:@_" };
local $SIG{__DIE__}  = sub { CORE::die  "DIE:@_"  };
BEGIN { $GLOBAL_SCALAR = 'begin-block'; }
UNITCHECK { $GLOBAL_HASH{unitcheck} = 1; }
CHECK { $GLOBAL_HASH{check} = 1; }
INIT { $GLOBAL_HASH{init} = 1; }
END { $GLOBAL_HASH{end} = time; }
=pod
=head1 NAME
Perl5::Lexer::Training::Corpus - artificial file for lexer stress testing
=head1 DESCRIPTION
This file intentionally mixes POD, packages, regexes, heredocs, formats,
special variables, prototypes, signatures, references, globs, tied variables,
quoted constructs, labels, loops, switch-like constructs, and data sections.
=over 4
=item * Not production code.
=item * Designed to contain many Perl 5 lexical forms.
=cut
package Lexer::Training::Base;
use strict;
use warnings;
our @ISA = qw(Exporter);
our @EXPORT_OK = qw(exported_one exported_two);
sub exported_one { return @_ ? $_[0] : undef }
sub exported_two ($left, $right = 0) { return $left + $right }
sub as_string { return ref($_[0]) . "(" . ($_[0]->{name} // 'anon') . ")" }
sub as_number { return 0 + ($_[0]->{number} // 0) }
package Lexer::Training::TieHash;
sub TIEHASH  { my ($class, %seed) = @_; bless { data => \%seed, iter => [] }, $class }
sub FETCH    { my ($self, $key) = @_; return $self->{data}{$key} }
sub STORE    { my ($self, $key, $val) = @_; $self->{data}{$key} = $val }
sub DELETE   { my ($self, $key) = @_; delete $self->{data}{$key} }
sub CLEAR    { my ($self) = @_; %{ $self->{data} } = () }
sub EXISTS   { my ($self, $key) = @_; exists $self->{data}{$key} }
sub FIRSTKEY { my ($self) = @_; $self->{iter} = [ keys %{ $self->{data} } ]; shift @{ $self->{iter} } }
sub NEXTKEY  { my ($self, $last) = @_; shift @{ $self->{iter} } }
sub SCALAR   { my ($self) = @_; scalar %{ $self->{data} } }
package Lexer::Training::Object;
use parent 'Lexer::Training::Base';
our $AUTOLOAD;
sub new ($class, %args) { bless { %args, slots => [], meta => {} }, $class }
sub AUTOLOAD { my ($self, @args) = @_; my $name = $AUTOLOAD =~ s/.*:://r; $self->{meta}{$name} = \@args; return $self }
sub DESTROY { my ($self) = @_; $self->{destroyed} = 1 }
sub method_with_proto ($$@) { my ($self, $first, @rest) = @_; return wantarray ? ($first, @rest) : $first }
sub lvalue_slot : lvalue { $_[0]->{slots}[ $_[1] // 0 ] }
package main;
use strict;
use warnings;
use Scalar::Util qw(blessed refaddr weaken);
use List::Util qw(first reduce sum0);
use POSIX qw(strftime);
my $scalar = "double quoted\nstring\twith \x{263A} and @{[ 'interpolation' ]}";
my $single = 'single quoted: backslash stays mostly literal except \\ and \'';
my $q      = q{q-braces with { nested looking } braces};
my $qq     = qq[q q bracket with $scalar and \N{LATIN CAPITAL LETTER A}];
my @array  = (0, 1, 2, 3, 'x', undef, \$scalar, [qw(a b c)], { k => 'v' });
my %hash   = (
    alpha => 1,
    beta  => [ map { $_ * 2 } 1 .. 5 ],
    gamma => { nested => { deeply => [ qw(one two three) ] } },
    delta => sub { my ($x) = @_; return $x ** 2 },
);
my $aref = \@array;
my $href = \%hash;
my $cref = sub ($x, $y = 1) { return $x + $y };
my $gref = \*STDOUT;
my $glob_alias = *GLOBAL_ALIAS = *scalar;
our $package_var = ${ __PACKAGE__ . '::VERSION' } // 'no-version';
local $/ = "\n";
local $\ = undef;
local $, = ",";
local $| = 1;
my $pid = $$;
my $program = $0;
my $os = $^O;
my $eval_error = $@;
my $errno = $!;
my $child_error = $?;
my $real_gid = $(;
my $effective_gid = $);
my $real_uid = $<;
my $effective_uid = $>;
my $input_line_number = $.;
my $last_match_start = $-[0] // -1;
my $last_match_end   = $+[0] // -1;
my @words = qw(alpha beta gamma delta epsilon);
my $regex1 = qr/\A (?<word> [A-Za-z_]\w* ) \s* = \s* (?<value> .+? ) \z/x;
my $regex2 = qr{ (?<!\\) / (?: \\\\/ | [^/])* / [imsxpodualngc]* }x;
my $cmd_output = qx{printf '%s\n' lexer_training};
my $translit = ($scalar =~ tr/a-z/A-Z/r);
$scalar =~ s{(?<vowel>[aeiou])}{uc($+{vowel})}eg;
$scalar =~ y/äöü/ÄÖÜ/;
my @split = split /(?:,|\s+)+/, "a, b c\t d";
my @grep  = grep { defined $_ && $_ =~ /\w/ } @split;
my @map   = map  { $_ => length($_) } @grep;
my %length_by_word = @map;
my $plain_heredoc = <<PLAIN_TEXT;
This is an unquoted heredoc.
It contains $scalar interpolation, backslashes \\, and tabs.
PLAIN_TEXT
my $single_heredoc = <<'SINGLE_TEXT';
This is a single-quoted heredoc.
$scalar is not interpolated here.
SINGLE_TEXT
my $double_heredoc = <<"DOUBLE_TEXT";
This is a double-quoted heredoc with @{[ scalar @array ]} interpolated count.
DOUBLE_TEXT
my $indented_heredoc = <<~'INDENTED_TEXT';
    Indented heredoc line one.
    Indented heredoc line two.
INDENTED_TEXT
OUTER: for my $i (0 .. 4) {
    INNER: foreach my $j (reverse 0 .. 4) {
        next INNER if $i == $j;
        last OUTER if $i * $j > 12;
        redo INNER if 0;
        if ($i < $j && ($i % 2 == 0 || $j % 2 == 1)) {
            $hash{pairs}{$i}{$j} = [$i, $j, $i <=> $j, $i cmp $j];
        } elsif ($i == 3) {
            $hash{elsif_seen}++;
        } else {
            $hash{else_seen} //= 0;
            $hash{else_seen} += 1;
        }
    } continue {
        $hash{continue_inner}++;
    }
} continue {
    $hash{continue_outer}++;
}
my $counter = 0;
while ($counter < 3) {
    $counter++;
    next if $counter == 1;
}
until ($counter >= 5) {
    ++$counter;
}
do {
    $counter--;
} while $counter > 2;
my $ternary = $counter > 1 ? "many" : "few";
my $defined_or = $hash{missing} //= 'default';
my $logical_mix = ($counter && $ternary) || !$defined_or;
given ($ternary) {
    when ('many') { $hash{switch} = 'many' }
    when (/few/)  { $hash{switch} = 'few'  }
    default       { $hash{switch} = 'none' }
}
my $safe = eval {
    die "synthetic exception" if 0;
    return 42;
};
if (my $error = $@) {
    warn "eval failed: $error";
}
try {
    my $z = 1 / ($counter || 1);
    $hash{try_value} = $z;
} catch ($e) {
    warn "try failed: $e";
}
open my $memory_fh, '<', \$plain_heredoc or die "open scalar ref failed: $!";
while (defined(my $line = <$memory_fh>)) {
    chomp $line;
    pos($line) = 0;
    $hash{io_lines}++;
}
seek $memory_fh, 0, 0;
my $where = tell $memory_fh;
close $memory_fh;
my @stat = stat __FILE__;
my $size = -s _;
my $readable = -r _;
my $writable = -w __FILE__;
my $executable = -x __FILE__;
format STDOUT =
@<<<<<<<<<<<<<<<<<<<< @||||| @>>>>>
"name",              "mid",  "right"
.
format REPORT_TOP =
Report title: @<<<<<<<<<<<<
$program
.
tie my %tied, 'Lexer::Training::TieHash', one => 1, two => 2;
$tied{three} = 3;
my $exists = exists $tied{one};
delete $tied{two};
my $object = Lexer::Training::Object->new(name => 'demo', number => 7);
$object->lvalue_slot(0) = "assigned through lvalue sub";
weaken($object->{weak_self} = $object);
my $math = (((1 + 2) * 3 - 4) / 5) % 6;
$math **= 2;
$math <<= 1;
$math >>= 1;
$math &= 0xff;
$math |= 0x10;
$math ^= 0x01;
my $concat = "a" . "b" x 3;
my $range_scalar = scalar(1 .. 10);
my @range_alpha = 'aa' .. 'az';
my $spaceship = 10 <=> 20;
my $str_cmp = "abc" cmp "abd";
my $match_state = (1 .. 3) ? "range-op" : "false";
my @sorted =
    sort { $a->{name} cmp $b->{name} || $a->{id} <=> $b->{id} }
    map  { +{ id => $_, name => sprintf("name_%03d", $_), odd => $_ % 2 } }
    grep { $_ % 3 != 0 }
    1 .. 30;
my $reducer = reduce { $a + $b->{id} } 0, @sorted;
my $finder = first { $_->{odd} } @sorted;
my $sum = sum0 map { $_->{id} } @sorted;
my $packed = pack("C n N a*", 65, 0x4243, 0x44454647, "tail");
my @unpacked = unpack("C n N a*", $packed);
vec($packed, 0, 8) = ord('Z');
substr($packed, 1, 2, "xy");
my $idx = index($packed, "tail");
my $ridx = rindex($packed, "x");
my $char = chr(ord("A") + 1);
my $folded = fc("Straße");
{
    no strict 'refs';
    my $symbol_name = "dynamic_scalar";
    ${"main::$symbol_name"} = "created through symbol table";
    *{"main::dynamic_sub"} = sub { return "dynamic sub called" };
    my $call = &{"main::dynamic_sub"}();
}
my $block_001 = {
    id => 1,
    name => qq{block_001_$scalar},
    list => [ map { { value => $_, square => $_ * $_, text => qq[item_${_}] } } 1 .. 6 ],
    regex => qr/(?<block>b001)\s*[:=]\s*(?<num>\d+)/x,
    code => sub ($x = 1, @rest) {
        my %local = (x => $x, rest => \@rest, seen => {});
        BLOCK_001: for my $outer (1 .. 3) {
            for my $inner (0 .. 2) {
                next BLOCK_001 if $outer == 2 && $inner == 1;
                $local{seen}{qq{$outer:$inner}} = [$outer, $inner, $outer + $inner];
                if (my ($m) = qq{b001:$outer} =~ /b001:(\d+)/) {
                    $local{match} //= $m;
                } elsif ($inner =~ /0/) {
                    $local{zero}++;
                } else {
                    $local{other} .= q{.};
                }
            } continue {
                $local{continued}++;
            }
        }
        return wantarray ? %local : \%local;
    },
};
push @array, $block_001;
$hash{generated}{1} = $block_001->{code}->(1 * 2, qw(extra tokens));
my $block_002 = {
    id => 2,
    name => qq{block_002_$scalar},
    list => [ map { { value => $_, square => $_ * $_, text => qq[item_${_}] } } 2 .. 7 ],
    regex => qr/(?<block>b002)\s*[:=]\s*(?<num>\d+)/x,
    code => sub ($x = 2, @rest) {
        my %local = (x => $x, rest => \@rest, seen => {});
        BLOCK_002: for my $outer (2 .. 4) {
            for my $inner (0 .. 2) {
                next BLOCK_002 if $outer == 3 && $inner == 1;
                $local{seen}{qq{$outer:$inner}} = [$outer, $inner, $outer + $inner];
                if (my ($m) = qq{b002:$outer} =~ /b002:(\d+)/) {
                    $local{match} //= $m;
                } elsif ($inner =~ /0/) {
                    $local{zero}++;
                } else {
                    $local{other} .= q{.};
                }
            } continue {
                $local{continued}++;
            }
        }
        return wantarray ? %local : \%local;
    },
};
push @array, $block_002;
$hash{generated}{2} = $block_002->{code}->(2 * 2, qw(extra tokens));
my $block_003 = {
    id => 3,
    name => qq{block_003_$scalar},
    list => [ map { { value => $_, square => $_ * $_, text => qq[item_${_}] } } 3 .. 8 ],
    regex => qr/(?<block>b003)\s*[:=]\s*(?<num>\d+)/x,
    code => sub ($x = 3, @rest) {
        my %local = (x => $x, rest => \@rest, seen => {});
        BLOCK_003: for my $outer (3 .. 5) {
            for my $inner (0 .. 2) {
                next BLOCK_003 if $outer == 4 && $inner == 1;
                $local{seen}{qq{$outer:$inner}} = [$outer, $inner, $outer + $inner];
                if (my ($m) = qq{b003:$outer} =~ /b003:(\d+)/) {
                    $local{match} //= $m;
                } elsif ($inner =~ /0/) {
                    $local{zero}++;
                } else {
                    $local{other} .= q{.};
                }
            } continue {
                $local{continued}++;
            }
        }
        return wantarray ? %local : \%local;
    },
};
push @array, $block_003;
$hash{generated}{3} = $block_003->{code}->(3 * 2, qw(extra tokens));
my $block_004 = {
    id => 4,
    name => qq{block_004_$scalar},
    list => [ map { { value => $_, square => $_ * $_, text => qq[item_${_}] } } 4 .. 9 ],
    regex => qr/(?<block>b004)\s*[:=]\s*(?<num>\d+)/x,
    code => sub ($x = 4, @rest) {
        my %local = (x => $x, rest => \@rest, seen => {});
        BLOCK_004: for my $outer (4 .. 6) {
            for my $inner (0 .. 2) {
                next BLOCK_004 if $outer == 5 && $inner == 1;
                $local{seen}{qq{$outer:$inner}} = [$outer, $inner, $outer + $inner];
                if (my ($m) = qq{b004:$outer} =~ /b004:(\d+)/) {
                    $local{match} //= $m;
                } elsif ($inner =~ /0/) {
                    $local{zero}++;
                } else {
                    $local{other} .= q{.};
                }
            } continue {
                $local{continued}++;
            }
        }
        return wantarray ? %local : \%local;
    },
};
push @array, $block_004;
$hash{generated}{4} = $block_004->{code}->(4 * 2, qw(extra tokens));
my $block_005 = {
    id => 5,
    name => qq{block_005_$scalar},
    list => [ map { { value => $_, square => $_ * $_, text => qq[item_${_}] } } 5 .. 10 ],
    regex => qr/(?<block>b005)\s*[:=]\s*(?<num>\d+)/x,
    code => sub ($x = 5, @rest) {
        my %local = (x => $x, rest => \@rest, seen => {});
        BLOCK_005: for my $outer (5 .. 7) {
            for my $inner (0 .. 2) {
                next BLOCK_005 if $outer == 6 && $inner == 1;
                $local{seen}{qq{$outer:$inner}} = [$outer, $inner, $outer + $inner];
                if (my ($m) = qq{b005:$outer} =~ /b005:(\d+)/) {
                    $local{match} //= $m;
                } elsif ($inner =~ /0/) {
                    $local{zero}++;
                } else {
                    $local{other} .= q{.};
                }
            } continue {
                $local{continued}++;
            }
        }
        return wantarray ? %local : \%local;
    },
};
push @array, $block_005;
$hash{generated}{5} = $block_005->{code}->(5 * 2, qw(extra tokens));
my $block_006 = {
    id => 6,
    name => qq{block_006_$scalar},
    list => [ map { { value => $_, square => $_ * $_, text => qq[item_${_}] } } 6 .. 11 ],
    regex => qr/(?<block>b006)\s*[:=]\s*(?<num>\d+)/x,
    code => sub ($x = 6, @rest) {
        my %local = (x => $x, rest => \@rest, seen => {});
        BLOCK_006: for my $outer (6 .. 8) {
            for my $inner (0 .. 2) {
                next BLOCK_006 if $outer == 7 && $inner == 1;
                $local{seen}{qq{$outer:$inner}} = [$outer, $inner, $outer + $inner];
                if (my ($m) = qq{b006:$outer} =~ /b006:(\d+)/) {
                    $local{match} //= $m;
                } elsif ($inner =~ /0/) {
                    $local{zero}++;
                } else {
                    $local{other} .= q{.};
                }
            } continue {
                $local{continued}++;
            }
        }
        return wantarray ? %local : \%local;
    },
};
push @array, $block_006;
$hash{generated}{6} = $block_006->{code}->(6 * 2, qw(extra tokens));
my $block_007 = {
    id => 7,
    name => qq{block_007_$scalar},
    list => [ map { { value => $_, square => $_ * $_, text => qq[item_${_}] } } 7 .. 12 ],
    regex => qr/(?<block>b007)\s*[:=]\s*(?<num>\d+)/x,
    code => sub ($x = 7, @rest) {
        my %local = (x => $x, rest => \@rest, seen => {});
        BLOCK_007: for my $outer (7 .. 9) {
            for my $inner (0 .. 2) {
                next BLOCK_007 if $outer == 8 && $inner == 1;
                $local{seen}{qq{$outer:$inner}} = [$outer, $inner, $outer + $inner];
                if (my ($m) = qq{b007:$outer} =~ /b007:(\d+)/) {
                    $local{match} //= $m;
                } elsif ($inner =~ /0/) {
                    $local{zero}++;
                } else {
                    $local{other} .= q{.};
                }
            } continue {
                $local{continued}++;
            }
        }
        return wantarray ? %local : \%local;
    },
};
push @array, $block_007;
$hash{generated}{7} = $block_007->{code}->(7 * 2, qw(extra tokens));
my $block_008 = {
    id => 8,
    name => qq{block_008_$scalar},
    list => [ map { { value => $_, square => $_ * $_, text => qq[item_${_}] } } 8 .. 13 ],
    regex => qr/(?<block>b008)\s*[:=]\s*(?<num>\d+)/x,
    code => sub ($x = 8, @rest) {
        my %local = (x => $x, rest => \@rest, seen => {});
        BLOCK_008: for my $outer (8 .. 10) {
            for my $inner (0 .. 2) {
                next BLOCK_008 if $outer == 9 && $inner == 1;
                $local{seen}{qq{$outer:$inner}} = [$outer, $inner, $outer + $inner];
                if (my ($m) = qq{b008:$outer} =~ /b008:(\d+)/) {
                    $local{match} //= $m;
                } elsif ($inner =~ /0/) {
                    $local{zero}++;
                } else {
                    $local{other} .= q{.};
                }
            } continue {
                $local{continued}++;
            }
        }
        return wantarray ? %local : \%local;
    },
};
push @array, $block_008;
$hash{generated}{8} = $block_008->{code}->(8 * 2, qw(extra tokens));
my $block_009 = {
    id => 9,
    name => qq{block_009_$scalar},
    list => [ map { { value => $_, square => $_ * $_, text => qq[item_${_}] } } 9 .. 14 ],
    regex => qr/(?<block>b009)\s*[:=]\s*(?<num>\d+)/x,
    code => sub ($x = 9, @rest) {
        my %local = (x => $x, rest => \@rest, seen => {});
        BLOCK_009: for my $outer (9 .. 11) {
            for my $inner (0 .. 2) {
                next BLOCK_009 if $outer == 10 && $inner == 1;
                $local{seen}{qq{$outer:$inner}} = [$outer, $inner, $outer + $inner];
                if (my ($m) = qq{b009:$outer} =~ /b009:(\d+)/) {
                    $local{match} //= $m;
                } elsif ($inner =~ /0/) {
                    $local{zero}++;
                } else {
                    $local{other} .= q{.};
                }
            } continue {
                $local{continued}++;
            }
        }
        return wantarray ? %local : \%local;
    },
};
push @array, $block_009;
$hash{generated}{9} = $block_009->{code}->(9 * 2, qw(extra tokens));
my $block_010 = {
    id => 10,
    name => qq{block_010_$scalar},
    list => [ map { { value => $_, square => $_ * $_, text => qq[item_${_}] } } 10 .. 15 ],
    regex => qr/(?<block>b010)\s*[:=]\s*(?<num>\d+)/x,
    code => sub ($x = 10, @rest) {
        my %local = (x => $x, rest => \@rest, seen => {});
        BLOCK_010: for my $outer (10 .. 12) {
            for my $inner (0 .. 2) {
                next BLOCK_010 if $outer == 11 && $inner == 1;
                $local{seen}{qq{$outer:$inner}} = [$outer, $inner, $outer + $inner];
                if (my ($m) = qq{b010:$outer} =~ /b010:(\d+)/) {
                    $local{match} //= $m;
                } elsif ($inner =~ /0/) {
                    $local{zero}++;
                } else {
                    $local{other} .= q{.};
                }
            } continue {
                $local{continued}++;
            }
        }
        return wantarray ? %local : \%local;
    },
};
push @array, $block_010;
$hash{generated}{10} = $block_010->{code}->(10 * 2, qw(extra tokens));
my $block_011 = {
    id => 11,
    name => qq{block_011_$scalar},
    list => [ map { { value => $_, square => $_ * $_, text => qq[item_${_}] } } 11 .. 16 ],
    regex => qr/(?<block>b011)\s*[:=]\s*(?<num>\d+)/x,
    code => sub ($x = 11, @rest) {
        my %local = (x => $x, rest => \@rest, seen => {});
        BLOCK_011: for my $outer (11 .. 13) {
            for my $inner (0 .. 2) {
                next BLOCK_011 if $outer == 12 && $inner == 1;
                $local{seen}{qq{$outer:$inner}} = [$outer, $inner, $outer + $inner];
                if (my ($m) = qq{b011:$outer} =~ /b011:(\d+)/) {
                    $local{match} //= $m;
                } elsif ($inner =~ /0/) {
                    $local{zero}++;
                } else {
                    $local{other} .= q{.};
                }
            } continue {
                $local{continued}++;
            }
        }
        return wantarray ? %local : \%local;
    },
};
push @array, $block_011;
$hash{generated}{11} = $block_011->{code}->(11 * 2, qw(extra tokens));
my $block_012 = {
    id => 12,
    name => qq{block_012_$scalar},
    list => [ map { { value => $_, square => $_ * $_, text => qq[item_${_}] } } 12 .. 17 ],
    regex => qr/(?<block>b012)\s*[:=]\s*(?<num>\d+)/x,
    code => sub ($x = 12, @rest) {
        my %local = (x => $x, rest => \@rest, seen => {});
        BLOCK_012: for my $outer (12 .. 14) {
            for my $inner (0 .. 2) {
                next BLOCK_012 if $outer == 13 && $inner == 1;
                $local{seen}{qq{$outer:$inner}} = [$outer, $inner, $outer + $inner];
                if (my ($m) = qq{b012:$outer} =~ /b012:(\d+)/) {
                    $local{match} //= $m;
                } elsif ($inner =~ /0/) {
                    $local{zero}++;
                } else {
                    $local{other} .= q{.};
                }
            } continue {
                $local{continued}++;
            }
        }
        return wantarray ? %local : \%local;
    },
};
push @array, $block_012;
$hash{generated}{12} = $block_012->{code}->(12 * 2, qw(extra tokens));
my $block_013 = {
    id => 13,
    name => qq{block_013_$scalar},
    list => [ map { { value => $_, square => $_ * $_, text => qq[item_${_}] } } 13 .. 18 ],
    regex => qr/(?<block>b013)\s*[:=]\s*(?<num>\d+)/x,
    code => sub ($x = 13, @rest) {
        my %local = (x => $x, rest => \@rest, seen => {});
        BLOCK_013: for my $outer (13 .. 15) {
            for my $inner (0 .. 2) {
                next BLOCK_013 if $outer == 14 && $inner == 1;
                $local{seen}{qq{$outer:$inner}} = [$outer, $inner, $outer + $inner];
                if (my ($m) = qq{b013:$outer} =~ /b013:(\d+)/) {
                    $local{match} //= $m;
                } elsif ($inner =~ /0/) {
                    $local{zero}++;
                } else {
                    $local{other} .= q{.};
                }
            } continue {
                $local{continued}++;
            }
        }
        return wantarray ? %local : \%local;
    },
};
push @array, $block_013;
$hash{generated}{13} = $block_013->{code}->(13 * 2, qw(extra tokens));
my $block_014 = {
    id => 14,
    name => qq{block_014_$scalar},
    list => [ map { { value => $_, square => $_ * $_, text => qq[item_${_}] } } 14 .. 19 ],
    regex => qr/(?<block>b014)\s*[:=]\s*(?<num>\d+)/x,
    code => sub ($x = 14, @rest) {
        my %local = (x => $x, rest => \@rest, seen => {});
        BLOCK_014: for my $outer (14 .. 16) {
            for my $inner (0 .. 2) {
                next BLOCK_014 if $outer == 15 && $inner == 1;
                $local{seen}{qq{$outer:$inner}} = [$outer, $inner, $outer + $inner];
                if (my ($m) = qq{b014:$outer} =~ /b014:(\d+)/) {
                    $local{match} //= $m;
                } elsif ($inner =~ /0/) {
                    $local{zero}++;
                } else {
                    $local{other} .= q{.};
                }
            } continue {
                $local{continued}++;
            }
        }
        return wantarray ? %local : \%local;
    },
};
push @array, $block_014;
$hash{generated}{14} = $block_014->{code}->(14 * 2, qw(extra tokens));
my $block_015 = {
    id => 15,
    name => qq{block_015_$scalar},
    list => [ map { { value => $_, square => $_ * $_, text => qq[item_${_}] } } 15 .. 20 ],
    regex => qr/(?<block>b015)\s*[:=]\s*(?<num>\d+)/x,
    code => sub ($x = 15, @rest) {
        my %local = (x => $x, rest => \@rest, seen => {});
        BLOCK_015: for my $outer (15 .. 17) {
            for my $inner (0 .. 2) {
                next BLOCK_015 if $outer == 16 && $inner == 1;
                $local{seen}{qq{$outer:$inner}} = [$outer, $inner, $outer + $inner];
                if (my ($m) = qq{b015:$outer} =~ /b015:(\d+)/) {
                    $local{match} //= $m;
                } elsif ($inner =~ /0/) {
                    $local{zero}++;
                } else {
                    $local{other} .= q{.};
                }
            } continue {
                $local{continued}++;
            }
        }
        return wantarray ? %local : \%local;
    },
};
push @array, $block_015;
$hash{generated}{15} = $block_015->{code}->(15 * 2, qw(extra tokens));
my $block_016 = {
    id => 16,
    name => qq{block_016_$scalar},
    list => [ map { { value => $_, square => $_ * $_, text => qq[item_${_}] } } 16 .. 21 ],
    regex => qr/(?<block>b016)\s*[:=]\s*(?<num>\d+)/x,
    code => sub ($x = 16, @rest) {
        my %local = (x => $x, rest => \@rest, seen => {});
        BLOCK_016: for my $outer (16 .. 18) {
            for my $inner (0 .. 2) {
                next BLOCK_016 if $outer == 17 && $inner == 1;
                $local{seen}{qq{$outer:$inner}} = [$outer, $inner, $outer + $inner];
                if (my ($m) = qq{b016:$outer} =~ /b016:(\d+)/) {
                    $local{match} //= $m;
                } elsif ($inner =~ /0/) {
                    $local{zero}++;
                } else {
                    $local{other} .= q{.};
                }
            } continue {
                $local{continued}++;
            }
        }
        return wantarray ? %local : \%local;
    },
};
push @array, $block_016;
$hash{generated}{16} = $block_016->{code}->(16 * 2, qw(extra tokens));
my $block_017 = {
    id => 17,
    name => qq{block_017_$scalar},
    list => [ map { { value => $_, square => $_ * $_, text => qq[item_${_}] } } 17 .. 22 ],
    regex => qr/(?<block>b017)\s*[:=]\s*(?<num>\d+)/x,
    code => sub ($x = 17, @rest) {
        my %local = (x => $x, rest => \@rest, seen => {});
        BLOCK_017: for my $outer (17 .. 19) {
            for my $inner (0 .. 2) {
                next BLOCK_017 if $outer == 18 && $inner == 1;
                $local{seen}{qq{$outer:$inner}} = [$outer, $inner, $outer + $inner];
                if (my ($m) = qq{b017:$outer} =~ /b017:(\d+)/) {
                    $local{match} //= $m;
                } elsif ($inner =~ /0/) {
                    $local{zero}++;
                } else {
                    $local{other} .= q{.};
                }
            } continue {
                $local{continued}++;
            }
        }
        return wantarray ? %local : \%local;
    },
};
push @array, $block_017;
$hash{generated}{17} = $block_017->{code}->(17 * 2, qw(extra tokens));
my $block_018 = {
    id => 18,
    name => qq{block_018_$scalar},
    list => [ map { { value => $_, square => $_ * $_, text => qq[item_${_}] } } 18 .. 23 ],
    regex => qr/(?<block>b018)\s*[:=]\s*(?<num>\d+)/x,
    code => sub ($x = 18, @rest) {
        my %local = (x => $x, rest => \@rest, seen => {});
        BLOCK_018: for my $outer (18 .. 20) {
            for my $inner (0 .. 2) {
                next BLOCK_018 if $outer == 19 && $inner == 1;
                $local{seen}{qq{$outer:$inner}} = [$outer, $inner, $outer + $inner];
                if (my ($m) = qq{b018:$outer} =~ /b018:(\d+)/) {
                    $local{match} //= $m;
                } elsif ($inner =~ /0/) {
                    $local{zero}++;
                } else {
                    $local{other} .= q{.};
                }
            } continue {
                $local{continued}++;
            }
        }
        return wantarray ? %local : \%local;
    },
};
push @array, $block_018;
$hash{generated}{18} = $block_018->{code}->(18 * 2, qw(extra tokens));
my $block_019 = {
    id => 19,
    name => qq{block_019_$scalar},
    list => [ map { { value => $_, square => $_ * $_, text => qq[item_${_}] } } 19 .. 24 ],
    regex => qr/(?<block>b019)\s*[:=]\s*(?<num>\d+)/x,
    code => sub ($x = 19, @rest) {
        my %local = (x => $x, rest => \@rest, seen => {});
        BLOCK_019: for my $outer (19 .. 21) {
            for my $inner (0 .. 2) {
                next BLOCK_019 if $outer == 20 && $inner == 1;
                $local{seen}{qq{$outer:$inner}} = [$outer, $inner, $outer + $inner];
                if (my ($m) = qq{b019:$outer} =~ /b019:(\d+)/) {
                    $local{match} //= $m;
                } elsif ($inner =~ /0/) {
                    $local{zero}++;
                } else {
                    $local{other} .= q{.};
                }
            } continue {
                $local{continued}++;
            }
        }
        return wantarray ? %local : \%local;
    },
};
push @array, $block_019;
$hash{generated}{19} = $block_019->{code}->(19 * 2, qw(extra tokens));
my $block_020 = {
    id => 20,
    name => qq{block_020_$scalar},
    list => [ map { { value => $_, square => $_ * $_, text => qq[item_${_}] } } 20 .. 25 ],
    regex => qr/(?<block>b020)\s*[:=]\s*(?<num>\d+)/x,
    code => sub ($x = 20, @rest) {
        my %local = (x => $x, rest => \@rest, seen => {});
        BLOCK_020: for my $outer (20 .. 22) {
            for my $inner (0 .. 2) {
                next BLOCK_020 if $outer == 21 && $inner == 1;
                $local{seen}{qq{$outer:$inner}} = [$outer, $inner, $outer + $inner];
                if (my ($m) = qq{b020:$outer} =~ /b020:(\d+)/) {
                    $local{match} //= $m;
                } elsif ($inner =~ /0/) {
                    $local{zero}++;
                } else {
                    $local{other} .= q{.};
                }
            } continue {
                $local{continued}++;
            }
        }
        return wantarray ? %local : \%local;
    },
};
push @array, $block_020;
$hash{generated}{20} = $block_020->{code}->(20 * 2, qw(extra tokens));
my $block_021 = {
    id => 21,
    name => qq{block_021_$scalar},
    list => [ map { { value => $_, square => $_ * $_, text => qq[item_${_}] } } 21 .. 26 ],
    regex => qr/(?<block>b021)\s*[:=]\s*(?<num>\d+)/x,
    code => sub ($x = 21, @rest) {
        my %local = (x => $x, rest => \@rest, seen => {});
        BLOCK_021: for my $outer (21 .. 23) {
            for my $inner (0 .. 2) {
                next BLOCK_021 if $outer == 22 && $inner == 1;
                $local{seen}{qq{$outer:$inner}} = [$outer, $inner, $outer + $inner];
                if (my ($m) = qq{b021:$outer} =~ /b021:(\d+)/) {
                    $local{match} //= $m;
                } elsif ($inner =~ /0/) {
                    $local{zero}++;
                } else {
                    $local{other} .= q{.};
                }
            } continue {
                $local{continued}++;
            }
        }
        return wantarray ? %local : \%local;
    },
};
push @array, $block_021;
$hash{generated}{21} = $block_021->{code}->(21 * 2, qw(extra tokens));
my $block_022 = {
    id => 22,
    name => qq{block_022_$scalar},
    list => [ map { { value => $_, square => $_ * $_, text => qq[item_${_}] } } 22 .. 27 ],
    regex => qr/(?<block>b022)\s*[:=]\s*(?<num>\d+)/x,
    code => sub ($x = 22, @rest) {
        my %local = (x => $x, rest => \@rest, seen => {});
        BLOCK_022: for my $outer (22 .. 24) {
            for my $inner (0 .. 2) {
                next BLOCK_022 if $outer == 23 && $inner == 1;
                $local{seen}{qq{$outer:$inner}} = [$outer, $inner, $outer + $inner];
                if (my ($m) = qq{b022:$outer} =~ /b022:(\d+)/) {
                    $local{match} //= $m;
                } elsif ($inner =~ /0/) {
                    $local{zero}++;
                } else {
                    $local{other} .= q{.};
                }
            } continue {
                $local{continued}++;
            }
        }
        return wantarray ? %local : \%local;
    },
};
push @array, $block_022;
$hash{generated}{22} = $block_022->{code}->(22 * 2, qw(extra tokens));
my $block_023 = {
    id => 23,
    name => qq{block_023_$scalar},
    list => [ map { { value => $_, square => $_ * $_, text => qq[item_${_}] } } 23 .. 28 ],
    regex => qr/(?<block>b023)\s*[:=]\s*(?<num>\d+)/x,
    code => sub ($x = 23, @rest) {
        my %local = (x => $x, rest => \@rest, seen => {});
        BLOCK_023: for my $outer (23 .. 25) {
            for my $inner (0 .. 2) {
                next BLOCK_023 if $outer == 24 && $inner == 1;
                $local{seen}{qq{$outer:$inner}} = [$outer, $inner, $outer + $inner];
                if (my ($m) = qq{b023:$outer} =~ /b023:(\d+)/) {
                    $local{match} //= $m;
                } elsif ($inner =~ /0/) {
                    $local{zero}++;
                } else {
                    $local{other} .= q{.};
                }
            } continue {
                $local{continued}++;
            }
        }
        return wantarray ? %local : \%local;
    },
};
push @array, $block_023;
$hash{generated}{23} = $block_023->{code}->(23 * 2, qw(extra tokens));
my $block_024 = {
    id => 24,
    name => qq{block_024_$scalar},
    list => [ map { { value => $_, square => $_ * $_, text => qq[item_${_}] } } 24 .. 29 ],
    regex => qr/(?<block>b024)\s*[:=]\s*(?<num>\d+)/x,
    code => sub ($x = 24, @rest) {
        my %local = (x => $x, rest => \@rest, seen => {});
        BLOCK_024: for my $outer (24 .. 26) {
            for my $inner (0 .. 2) {
                next BLOCK_024 if $outer == 25 && $inner == 1;
                $local{seen}{qq{$outer:$inner}} = [$outer, $inner, $outer + $inner];
                if (my ($m) = qq{b024:$outer} =~ /b024:(\d+)/) {
                    $local{match} //= $m;
                } elsif ($inner =~ /0/) {
                    $local{zero}++;
                } else {
                    $local{other} .= q{.};
                }
            } continue {
                $local{continued}++;
            }
        }
        return wantarray ? %local : \%local;
    },
};
push @array, $block_024;
$hash{generated}{24} = $block_024->{code}->(24 * 2, qw(extra tokens));
my $block_025 = {
    id => 25,
    name => qq{block_025_$scalar},
    list => [ map { { value => $_, square => $_ * $_, text => qq[item_${_}] } } 25 .. 30 ],
    regex => qr/(?<block>b025)\s*[:=]\s*(?<num>\d+)/x,
    code => sub ($x = 25, @rest) {
        my %local = (x => $x, rest => \@rest, seen => {});
        BLOCK_025: for my $outer (25 .. 27) {
            for my $inner (0 .. 2) {
                next BLOCK_025 if $outer == 26 && $inner == 1;
                $local{seen}{qq{$outer:$inner}} = [$outer, $inner, $outer + $inner];
                if (my ($m) = qq{b025:$outer} =~ /b025:(\d+)/) {
                    $local{match} //= $m;
                } elsif ($inner =~ /0/) {
                    $local{zero}++;
                } else {
                    $local{other} .= q{.};
                }
            } continue {
                $local{continued}++;
            }
        }
        return wantarray ? %local : \%local;
    },
};
push @array, $block_025;
$hash{generated}{25} = $block_025->{code}->(25 * 2, qw(extra tokens));
my $block_026 = {
    id => 26,
    name => qq{block_026_$scalar},
    list => [ map { { value => $_, square => $_ * $_, text => qq[item_${_}] } } 26 .. 31 ],
    regex => qr/(?<block>b026)\s*[:=]\s*(?<num>\d+)/x,
    code => sub ($x = 26, @rest) {
        my %local = (x => $x, rest => \@rest, seen => {});
        BLOCK_026: for my $outer (26 .. 28) {
            for my $inner (0 .. 2) {
                next BLOCK_026 if $outer == 27 && $inner == 1;
                $local{seen}{qq{$outer:$inner}} = [$outer, $inner, $outer + $inner];
                if (my ($m) = qq{b026:$outer} =~ /b026:(\d+)/) {
                    $local{match} //= $m;
                } elsif ($inner =~ /0/) {
                    $local{zero}++;
                } else {
                    $local{other} .= q{.};
                }
            } continue {
                $local{continued}++;
            }
        }
        return wantarray ? %local : \%local;
    },
};
push @array, $block_026;
$hash{generated}{26} = $block_026->{code}->(26 * 2, qw(extra tokens));
my $block_027 = {
    id => 27,
    name => qq{block_027_$scalar},
    list => [ map { { value => $_, square => $_ * $_, text => qq[item_${_}] } } 27 .. 32 ],
    regex => qr/(?<block>b027)\s*[:=]\s*(?<num>\d+)/x,
    code => sub ($x = 27, @rest) {
        my %local = (x => $x, rest => \@rest, seen => {});
        BLOCK_027: for my $outer (27 .. 29) {
            for my $inner (0 .. 2) {
                next BLOCK_027 if $outer == 28 && $inner == 1;
                $local{seen}{qq{$outer:$inner}} = [$outer, $inner, $outer + $inner];
                if (my ($m) = qq{b027:$outer} =~ /b027:(\d+)/) {
                    $local{match} //= $m;
                } elsif ($inner =~ /0/) {
                    $local{zero}++;
                } else {
                    $local{other} .= q{.};
                }
            } continue {
                $local{continued}++;
            }
        }
        return wantarray ? %local : \%local;
    },
};
push @array, $block_027;
$hash{generated}{27} = $block_027->{code}->(27 * 2, qw(extra tokens));
my $block_028 = {
    id => 28,
    name => qq{block_028_$scalar},
    list => [ map { { value => $_, square => $_ * $_, text => qq[item_${_}] } } 28 .. 33 ],
    regex => qr/(?<block>b028)\s*[:=]\s*(?<num>\d+)/x,
    code => sub ($x = 28, @rest) {
        my %local = (x => $x, rest => \@rest, seen => {});
        BLOCK_028: for my $outer (28 .. 30) {
            for my $inner (0 .. 2) {
                next BLOCK_028 if $outer == 29 && $inner == 1;
                $local{seen}{qq{$outer:$inner}} = [$outer, $inner, $outer + $inner];
                if (my ($m) = qq{b028:$outer} =~ /b028:(\d+)/) {
                    $local{match} //= $m;
                } elsif ($inner =~ /0/) {
                    $local{zero}++;
                } else {
                    $local{other} .= q{.};
                }
            } continue {
                $local{continued}++;
            }
        }
        return wantarray ? %local : \%local;
    },
};
push @array, $block_028;
$hash{generated}{28} = $block_028->{code}->(28 * 2, qw(extra tokens));
my $block_029 = {
    id => 29,
    name => qq{block_029_$scalar},
    list => [ map { { value => $_, square => $_ * $_, text => qq[item_${_}] } } 29 .. 34 ],
    regex => qr/(?<block>b029)\s*[:=]\s*(?<num>\d+)/x,
    code => sub ($x = 29, @rest) {
        my %local = (x => $x, rest => \@rest, seen => {});
        BLOCK_029: for my $outer (29 .. 31) {
            for my $inner (0 .. 2) {
                next BLOCK_029 if $outer == 30 && $inner == 1;
                $local{seen}{qq{$outer:$inner}} = [$outer, $inner, $outer + $inner];
                if (my ($m) = qq{b029:$outer} =~ /b029:(\d+)/) {
                    $local{match} //= $m;
                } elsif ($inner =~ /0/) {
                    $local{zero}++;
                } else {
                    $local{other} .= q{.};
                }
            } continue {
                $local{continued}++;
            }
        }
        return wantarray ? %local : \%local;
    },
};
push @array, $block_029;
$hash{generated}{29} = $block_029->{code}->(29 * 2, qw(extra tokens));
my $block_030 = {
    id => 30,
    name => qq{block_030_$scalar},
    list => [ map { { value => $_, square => $_ * $_, text => qq[item_${_}] } } 30 .. 35 ],
    regex => qr/(?<block>b030)\s*[:=]\s*(?<num>\d+)/x,
    code => sub ($x = 30, @rest) {
        my %local = (x => $x, rest => \@rest, seen => {});
        BLOCK_030: for my $outer (30 .. 32) {
            for my $inner (0 .. 2) {
                next BLOCK_030 if $outer == 31 && $inner == 1;
                $local{seen}{qq{$outer:$inner}} = [$outer, $inner, $outer + $inner];
                if (my ($m) = qq{b030:$outer} =~ /b030:(\d+)/) {
                    $local{match} //= $m;
                } elsif ($inner =~ /0/) {
                    $local{zero}++;
                } else {
                    $local{other} .= q{.};
                }
            } continue {
                $local{continued}++;
            }
        }
        return wantarray ? %local : \%local;
    },
};
push @array, $block_030;
$hash{generated}{30} = $block_030->{code}->(30 * 2, qw(extra tokens));
my $block_031 = {
    id => 31,
    name => qq{block_031_$scalar},
    list => [ map { { value => $_, square => $_ * $_, text => qq[item_${_}] } } 31 .. 36 ],
    regex => qr/(?<block>b031)\s*[:=]\s*(?<num>\d+)/x,
    code => sub ($x = 31, @rest) {
        my %local = (x => $x, rest => \@rest, seen => {});
        BLOCK_031: for my $outer (31 .. 33) {
            for my $inner (0 .. 2) {
                next BLOCK_031 if $outer == 32 && $inner == 1;
                $local{seen}{qq{$outer:$inner}} = [$outer, $inner, $outer + $inner];
                if (my ($m) = qq{b031:$outer} =~ /b031:(\d+)/) {
                    $local{match} //= $m;
                } elsif ($inner =~ /0/) {
                    $local{zero}++;
                } else {
                    $local{other} .= q{.};
                }
            } continue {
                $local{continued}++;
            }
        }
        return wantarray ? %local : \%local;
    },
};
push @array, $block_031;
$hash{generated}{31} = $block_031->{code}->(31 * 2, qw(extra tokens));
my $block_032 = {
    id => 32,
    name => qq{block_032_$scalar},
    list => [ map { { value => $_, square => $_ * $_, text => qq[item_${_}] } } 32 .. 37 ],
    regex => qr/(?<block>b032)\s*[:=]\s*(?<num>\d+)/x,
    code => sub ($x = 32, @rest) {
        my %local = (x => $x, rest => \@rest, seen => {});
        BLOCK_032: for my $outer (32 .. 34) {
            for my $inner (0 .. 2) {
                next BLOCK_032 if $outer == 33 && $inner == 1;
                $local{seen}{qq{$outer:$inner}} = [$outer, $inner, $outer + $inner];
                if (my ($m) = qq{b032:$outer} =~ /b032:(\d+)/) {
                    $local{match} //= $m;
                } elsif ($inner =~ /0/) {
                    $local{zero}++;
                } else {
                    $local{other} .= q{.};
                }
            } continue {
                $local{continued}++;
            }
        }
        return wantarray ? %local : \%local;
    },
};
push @array, $block_032;
$hash{generated}{32} = $block_032->{code}->(32 * 2, qw(extra tokens));
my $block_033 = {
    id => 33,
    name => qq{block_033_$scalar},
    list => [ map { { value => $_, square => $_ * $_, text => qq[item_${_}] } } 33 .. 38 ],
    regex => qr/(?<block>b033)\s*[:=]\s*(?<num>\d+)/x,
    code => sub ($x = 33, @rest) {
        my %local = (x => $x, rest => \@rest, seen => {});
        BLOCK_033: for my $outer (33 .. 35) {
            for my $inner (0 .. 2) {
                next BLOCK_033 if $outer == 34 && $inner == 1;
                $local{seen}{qq{$outer:$inner}} = [$outer, $inner, $outer + $inner];
                if (my ($m) = qq{b033:$outer} =~ /b033:(\d+)/) {
                    $local{match} //= $m;
                } elsif ($inner =~ /0/) {
                    $local{zero}++;
                } else {
                    $local{other} .= q{.};
                }
            } continue {
                $local{continued}++;
            }
        }
        return wantarray ? %local : \%local;
    },
};
push @array, $block_033;
$hash{generated}{33} = $block_033->{code}->(33 * 2, qw(extra tokens));
my $block_034 = {
    id => 34,
    name => qq{block_034_$scalar},
    list => [ map { { value => $_, square => $_ * $_, text => qq[item_${_}] } } 34 .. 39 ],
    regex => qr/(?<block>b034)\s*[:=]\s*(?<num>\d+)/x,
    code => sub ($x = 34, @rest) {
        my %local = (x => $x, rest => \@rest, seen => {});
        BLOCK_034: for my $outer (34 .. 36) {
            for my $inner (0 .. 2) {
                next BLOCK_034 if $outer == 35 && $inner == 1;
                $local{seen}{qq{$outer:$inner}} = [$outer, $inner, $outer + $inner];
                if (my ($m) = qq{b034:$outer} =~ /b034:(\d+)/) {
                    $local{match} //= $m;
                } elsif ($inner =~ /0/) {
                    $local{zero}++;
                } else {
                    $local{other} .= q{.};
                }
            } continue {
                $local{continued}++;
            }
        }
        return wantarray ? %local : \%local;
    },
};
push @array, $block_034;
$hash{generated}{34} = $block_034->{code}->(34 * 2, qw(extra tokens));
my $block_035 = {
    id => 35,
    name => qq{block_035_$scalar},
    list => [ map { { value => $_, square => $_ * $_, text => qq[item_${_}] } } 35 .. 40 ],
    regex => qr/(?<block>b035)\s*[:=]\s*(?<num>\d+)/x,
    code => sub ($x = 35, @rest) {
        my %local = (x => $x, rest => \@rest, seen => {});
        BLOCK_035: for my $outer (35 .. 37) {
            for my $inner (0 .. 2) {
                next BLOCK_035 if $outer == 36 && $inner == 1;
                $local{seen}{qq{$outer:$inner}} = [$outer, $inner, $outer + $inner];
                if (my ($m) = qq{b035:$outer} =~ /b035:(\d+)/) {
                    $local{match} //= $m;
                } elsif ($inner =~ /0/) {
                    $local{zero}++;
                } else {
                    $local{other} .= q{.};
                }
            } continue {
                $local{continued}++;
            }
        }
        return wantarray ? %local : \%local;
    },
};
push @array, $block_035;
$hash{generated}{35} = $block_035->{code}->(35 * 2, qw(extra tokens));
my $block_036 = {
    id => 36,
    name => qq{block_036_$scalar},
    list => [ map { { value => $_, square => $_ * $_, text => qq[item_${_}] } } 36 .. 41 ],
    regex => qr/(?<block>b036)\s*[:=]\s*(?<num>\d+)/x,
    code => sub ($x = 36, @rest) {
        my %local = (x => $x, rest => \@rest, seen => {});
        BLOCK_036: for my $outer (36 .. 38) {
            for my $inner (0 .. 2) {
                next BLOCK_036 if $outer == 37 && $inner == 1;
                $local{seen}{qq{$outer:$inner}} = [$outer, $inner, $outer + $inner];
                if (my ($m) = qq{b036:$outer} =~ /b036:(\d+)/) {
                    $local{match} //= $m;
                } elsif ($inner =~ /0/) {
                    $local{zero}++;
                } else {
                    $local{other} .= q{.};
                }
            } continue {
                $local{continued}++;
            }
        }
        return wantarray ? %local : \%local;
    },
};
push @array, $block_036;
$hash{generated}{36} = $block_036->{code}->(36 * 2, qw(extra tokens));
my $block_037 = {
    id => 37,
    name => qq{block_037_$scalar},
    list => [ map { { value => $_, square => $_ * $_, text => qq[item_${_}] } } 37 .. 42 ],
    regex => qr/(?<block>b037)\s*[:=]\s*(?<num>\d+)/x,
    code => sub ($x = 37, @rest) {
        my %local = (x => $x, rest => \@rest, seen => {});
        BLOCK_037: for my $outer (37 .. 39) {
            for my $inner (0 .. 2) {
                next BLOCK_037 if $outer == 38 && $inner == 1;
                $local{seen}{qq{$outer:$inner}} = [$outer, $inner, $outer + $inner];
                if (my ($m) = qq{b037:$outer} =~ /b037:(\d+)/) {
                    $local{match} //= $m;
                } elsif ($inner =~ /0/) {
                    $local{zero}++;
                } else {
                    $local{other} .= q{.};
                }
            } continue {
                $local{continued}++;
            }
        }
        return wantarray ? %local : \%local;
    },
};
push @array, $block_037;
$hash{generated}{37} = $block_037->{code}->(37 * 2, qw(extra tokens));
my $block_038 = {
    id => 38,
    name => qq{block_038_$scalar},
    list => [ map { { value => $_, square => $_ * $_, text => qq[item_${_}] } } 38 .. 43 ],
    regex => qr/(?<block>b038)\s*[:=]\s*(?<num>\d+)/x,
    code => sub ($x = 38, @rest) {
        my %local = (x => $x, rest => \@rest, seen => {});
        BLOCK_038: for my $outer (38 .. 40) {
            for my $inner (0 .. 2) {
                next BLOCK_038 if $outer == 39 && $inner == 1;
                $local{seen}{qq{$outer:$inner}} = [$outer, $inner, $outer + $inner];
                if (my ($m) = qq{b038:$outer} =~ /b038:(\d+)/) {
                    $local{match} //= $m;
                } elsif ($inner =~ /0/) {
                    $local{zero}++;
                } else {
                    $local{other} .= q{.};
                }
            } continue {
                $local{continued}++;
            }
        }
        return wantarray ? %local : \%local;
    },
};
push @array, $block_038;
$hash{generated}{38} = $block_038->{code}->(38 * 2, qw(extra tokens));
my $block_039 = {
    id => 39,
    name => qq{block_039_$scalar},
    list => [ map { { value => $_, square => $_ * $_, text => qq[item_${_}] } } 39 .. 44 ],
    regex => qr/(?<block>b039)\s*[:=]\s*(?<num>\d+)/x,
    code => sub ($x = 39, @rest) {
        my %local = (x => $x, rest => \@rest, seen => {});
        BLOCK_039: for my $outer (39 .. 41) {
            for my $inner (0 .. 2) {
                next BLOCK_039 if $outer == 40 && $inner == 1;
                $local{seen}{qq{$outer:$inner}} = [$outer, $inner, $outer + $inner];
                if (my ($m) = qq{b039:$outer} =~ /b039:(\d+)/) {
                    $local{match} //= $m;
                } elsif ($inner =~ /0/) {
                    $local{zero}++;
                } else {
                    $local{other} .= q{.};
                }
            } continue {
                $local{continued}++;
            }
        }
        return wantarray ? %local : \%local;
    },
};
push @array, $block_039;
$hash{generated}{39} = $block_039->{code}->(39 * 2, qw(extra tokens));
my $block_040 = {
    id => 40,
    name => qq{block_040_$scalar},
    list => [ map { { value => $_, square => $_ * $_, text => qq[item_${_}] } } 40 .. 45 ],
    regex => qr/(?<block>b040)\s*[:=]\s*(?<num>\d+)/x,
    code => sub ($x = 40, @rest) {
        my %local = (x => $x, rest => \@rest, seen => {});
        BLOCK_040: for my $outer (40 .. 42) {
            for my $inner (0 .. 2) {
                next BLOCK_040 if $outer == 41 && $inner == 1;
                $local{seen}{qq{$outer:$inner}} = [$outer, $inner, $outer + $inner];
                if (my ($m) = qq{b040:$outer} =~ /b040:(\d+)/) {
                    $local{match} //= $m;
                } elsif ($inner =~ /0/) {
                    $local{zero}++;
                } else {
                    $local{other} .= q{.};
                }
            } continue {
                $local{continued}++;
            }
        }
        return wantarray ? %local : \%local;
    },
};
push @array, $block_040;
$hash{generated}{40} = $block_040->{code}->(40 * 2, qw(extra tokens));
my $block_041 = {
    id => 41,
    name => qq{block_041_$scalar},
    list => [ map { { value => $_, square => $_ * $_, text => qq[item_${_}] } } 41 .. 46 ],
    regex => qr/(?<block>b041)\s*[:=]\s*(?<num>\d+)/x,
    code => sub ($x = 41, @rest) {
        my %local = (x => $x, rest => \@rest, seen => {});
        BLOCK_041: for my $outer (41 .. 43) {
            for my $inner (0 .. 2) {
                next BLOCK_041 if $outer == 42 && $inner == 1;
                $local{seen}{qq{$outer:$inner}} = [$outer, $inner, $outer + $inner];
                if (my ($m) = qq{b041:$outer} =~ /b041:(\d+)/) {
                    $local{match} //= $m;
                } elsif ($inner =~ /0/) {
                    $local{zero}++;
                } else {
                    $local{other} .= q{.};
                }
            } continue {
                $local{continued}++;
            }
        }
        return wantarray ? %local : \%local;
    },
};
push @array, $block_041;
$hash{generated}{41} = $block_041->{code}->(41 * 2, qw(extra tokens));
my $block_042 = {
    id => 42,
    name => qq{block_042_$scalar},
    list => [ map { { value => $_, square => $_ * $_, text => qq[item_${_}] } } 42 .. 47 ],
    regex => qr/(?<block>b042)\s*[:=]\s*(?<num>\d+)/x,
    code => sub ($x = 42, @rest) {
        my %local = (x => $x, rest => \@rest, seen => {});
        BLOCK_042: for my $outer (42 .. 44) {
            for my $inner (0 .. 2) {
                next BLOCK_042 if $outer == 43 && $inner == 1;
                $local{seen}{qq{$outer:$inner}} = [$outer, $inner, $outer + $inner];
                if (my ($m) = qq{b042:$outer} =~ /b042:(\d+)/) {
                    $local{match} //= $m;
                } elsif ($inner =~ /0/) {
                    $local{zero}++;
                } else {
                    $local{other} .= q{.};
                }
            } continue {
                $local{continued}++;
            }
        }
        return wantarray ? %local : \%local;
    },
};
push @array, $block_042;
$hash{generated}{42} = $block_042->{code}->(42 * 2, qw(extra tokens));
my $block_043 = {
    id => 43,
    name => qq{block_043_$scalar},
    list => [ map { { value => $_, square => $_ * $_, text => qq[item_${_}] } } 43 .. 48 ],
    regex => qr/(?<block>b043)\s*[:=]\s*(?<num>\d+)/x,
    code => sub ($x = 43, @rest) {
        my %local = (x => $x, rest => \@rest, seen => {});
        BLOCK_043: for my $outer (43 .. 45) {
            for my $inner (0 .. 2) {
                next BLOCK_043 if $outer == 44 && $inner == 1;
                $local{seen}{qq{$outer:$inner}} = [$outer, $inner, $outer + $inner];
                if (my ($m) = qq{b043:$outer} =~ /b043:(\d+)/) {
                    $local{match} //= $m;
                } elsif ($inner =~ /0/) {
                    $local{zero}++;
                } else {
                    $local{other} .= q{.};
                }
            } continue {
                $local{continued}++;
            }
        }
        return wantarray ? %local : \%local;
    },
};
push @array, $block_043;
$hash{generated}{43} = $block_043->{code}->(43 * 2, qw(extra tokens));
my $block_044 = {
    id => 44,
    name => qq{block_044_$scalar},
    list => [ map { { value => $_, square => $_ * $_, text => qq[item_${_}] } } 44 .. 49 ],
    regex => qr/(?<block>b044)\s*[:=]\s*(?<num>\d+)/x,
    code => sub ($x = 44, @rest) {
        my %local = (x => $x, rest => \@rest, seen => {});
        BLOCK_044: for my $outer (44 .. 46) {
            for my $inner (0 .. 2) {
                next BLOCK_044 if $outer == 45 && $inner == 1;
                $local{seen}{qq{$outer:$inner}} = [$outer, $inner, $outer + $inner];
                if (my ($m) = qq{b044:$outer} =~ /b044:(\d+)/) {
                    $local{match} //= $m;
                } elsif ($inner =~ /0/) {
                    $local{zero}++;
                } else {
                    $local{other} .= q{.};
                }
            } continue {
                $local{continued}++;
            }
        }
        return wantarray ? %local : \%local;
    },
};
push @array, $block_044;
$hash{generated}{44} = $block_044->{code}->(44 * 2, qw(extra tokens));
my $block_045 = {
    id => 45,
    name => qq{block_045_$scalar},
    list => [ map { { value => $_, square => $_ * $_, text => qq[item_${_}] } } 45 .. 50 ],
    regex => qr/(?<block>b045)\s*[:=]\s*(?<num>\d+)/x,
    code => sub ($x = 45, @rest) {
        my %local = (x => $x, rest => \@rest, seen => {});
        BLOCK_045: for my $outer (45 .. 47) {
            for my $inner (0 .. 2) {
                next BLOCK_045 if $outer == 46 && $inner == 1;
                $local{seen}{qq{$outer:$inner}} = [$outer, $inner, $outer + $inner];
                if (my ($m) = qq{b045:$outer} =~ /b045:(\d+)/) {
                    $local{match} //= $m;
                } elsif ($inner =~ /0/) {
                    $local{zero}++;
                } else {
                    $local{other} .= q{.};
                }
            } continue {
                $local{continued}++;
            }
        }
        return wantarray ? %local : \%local;
    },
};
push @array, $block_045;
$hash{generated}{45} = $block_045->{code}->(45 * 2, qw(extra tokens));
my $rx_angle  = qr< \A <tag> .* </tag> \z >x;
my $rx_bang   = qr!https?://[^/\s]+(?:/[^\s]*)?!;
my $rx_pipe   = qr|^ (?<path> / (?: [^/]+ /? )* ) $|x;
my $rx_hash   = qr# [#] [A-Fa-f0-9]{6} \b #x;
my $rx_paren  = qr( \( (?: [^()]++ | (?0) )* \) )x;
my $rx_square = qr[ \[ [^\]]* \] ]x;
my $rx_curly  = qr{ \{ (?: [^{}]++ | (?0) )* \} }x;
$scalar =~ m/(\w+)(?:\s+(\w+))?/gc;
$scalar =~ m{ (?<unicode> \p{Letter}+ ) }x;
$scalar =~ s!\b(block)\b!uc($1)!egi;
$scalar =~ s#(?<=\w)-(?=\w)#_#g;
$scalar =~ tr[0-9][０-９];
$scalar =~ y{A-Z}{a-z};
sub plain_sub { return join ":", @_ }
sub proto_sub ($@) { my ($first, @rest) = @_; return $first . ":" . @rest }
sub sig_sub ($first, $second = 'default', @tail) { return [$first, $second, @tail] }
sub attr_sub : method { return scalar @_ }
my $anon_recursive;
$anon_recursive = sub ($n) { return $n <= 1 ? 1 : $n * __SUB__->($n - 1) };
my $call_1 = plain_sub "no", "parentheses";
my $call_2 = proto_sub("with", qw(parentheses and list));
my $call_3 = &plain_sub(qw(ampersand call));
my $call_4 = $cref->(10, 20);
my $call_5 = ${ \$cref }->(30, 40);
my @slice_a = @array[0, 2, 4];
my @slice_h = @hash{qw(alpha beta gamma)};
my $post_array = $aref->@[0 .. 2];
my $post_hash  = $href->@{qw(alpha beta)};
my $deep_value = $href->{gamma}{nested}{deeply}[1];
$href->{gamma}->{nested}->{deeply}->@[0, 2] = qw(ONE THREE);
my $scalar_ref = \$scalar;
my $array_ref_ref = \\@array;
my $hash_ref_ref = \\%hash;
my $code_ref_ref = \\$cref;
my $glob_ref_ref = \\*STDERR;
${$scalar_ref} .= " changed";
push @{ ${$array_ref_ref} }, "through-ref-ref";
${$hash_ref_ref}->{through} = "hash-ref-ref";
${$code_ref_ref}->(1, 2);
print {$gref} "" if 0;
my $time = time;
my $gmtime = gmtime($time);
my $localtime = localtime($time);
my $formatted_time = strftime("%Y-%m-%dT%H:%M:%S%z", localtime($time));
my $random = rand 100;
srand 1234;
my $int = int $random;
my $sqrt = sqrt 144;
my $log = log 10;
my $exp = exp 1;
my $sin = sin 0;
my $cos = cos 0;
my $atan2 = atan2 1, 1;
my $hex = hex "ff";
my $oct = oct "0777";
my $sprintf = sprintf "%04d %0.2f %s", 7, PI, "pi";
my $reverse = reverse qw(a b c);
my $scalar_reverse = scalar reverse "abc";
my @keys = keys %hash;
my @values = values %hash;
my ($each_k, $each_v) = each %hash;
reset %hash;
if (exists $hash{generated}{1}) {
    my $tmp_46 = $hash{generated}{1};
    for my $key (sort keys %{ $tmp_46 }) {
        my $value = $tmp_46->{$key};
        given (ref $value) {
            when ('ARRAY') { push @array, @$value[0 .. $#$value > 1 ? 1 : $#$value] }
            when ('HASH')  { $hash{tail}{46} = { %$value } }
            default       { $hash{tail_scalar}{46} = defined($value) ? qq{$value} : undef }
        }
    }
} else {
    $hash{missing_generated}{46} = sub { return q{missing} =~ s/i/I/r };
}
if (exists $hash{generated}{2}) {
    my $tmp_47 = $hash{generated}{2};
    for my $key (sort keys %{ $tmp_47 }) {
        my $value = $tmp_47->{$key};
        given (ref $value) {
            when ('ARRAY') { push @array, @$value[0 .. $#$value > 1 ? 1 : $#$value] }
            when ('HASH')  { $hash{tail}{47} = { %$value } }
            default       { $hash{tail_scalar}{47} = defined($value) ? qq{$value} : undef }
        }
    }
} else {
    $hash{missing_generated}{47} = sub { return q{missing} =~ s/i/I/r };
}
if (exists $hash{generated}{3}) {
    my $tmp_48 = $hash{generated}{3};
    for my $key (sort keys %{ $tmp_48 }) {
        my $value = $tmp_48->{$key};
        given (ref $value) {
            when ('ARRAY') { push @array, @$value[0 .. $#$value > 1 ? 1 : $#$value] }
            when ('HASH')  { $hash{tail}{48} = { %$value } }
            default       { $hash{tail_scalar}{48} = defined($value) ? qq{$value} : undef }
        }
    }
} else {
    $hash{missing_generated}{48} = sub { return q{missing} =~ s/i/I/r };
}
if (exists $hash{generated}{4}) {
    my $tmp_49 = $hash{generated}{4};
    for my $key (sort keys %{ $tmp_49 }) {
        my $value = $tmp_49->{$key};
        given (ref $value) {
            when ('ARRAY') { push @array, @$value[0 .. $#$value > 1 ? 1 : $#$value] }
            when ('HASH')  { $hash{tail}{49} = { %$value } }
            default       { $hash{tail_scalar}{49} = defined($value) ? qq{$value} : undef }
        }
    }
} else {
    $hash{missing_generated}{49} = sub { return q{missing} =~ s/i/I/r };
}
if (exists $hash{generated}{5}) {
    my $tmp_50 = $hash{generated}{5};
    for my $key (sort keys %{ $tmp_50 }) {
        my $value = $tmp_50->{$key};
        given (ref $value) {
            when ('ARRAY') { push @array, @$value[0 .. $#$value > 1 ? 1 : $#$value] }
            when ('HASH')  { $hash{tail}{50} = { %$value } }
            default       { $hash{tail_scalar}{50} = defined($value) ? qq{$value} : undef }
        }
    }
} else {
    $hash{missing_generated}{50} = sub { return q{missing} =~ s/i/I/r };
}
if (exists $hash{generated}{6}) {
    my $tmp_51 = $hash{generated}{6};
    for my $key (sort keys %{ $tmp_51 }) {
        my $value = $tmp_51->{$key};
        given (ref $value) {
            when ('ARRAY') { push @array, @$value[0 .. $#$value > 1 ? 1 : $#$value] }
            when ('HASH')  { $hash{tail}{51} = { %$value } }
            default       { $hash{tail_scalar}{51} = defined($value) ? qq{$value} : undef }
        }
    }
} else {
    $hash{missing_generated}{51} = sub { return q{missing} =~ s/i/I/r };
}
if (exists $hash{generated}{7}) {
    my $tmp_52 = $hash{generated}{7};
    for my $key (sort keys %{ $tmp_52 }) {
        my $value = $tmp_52->{$key};
        given (ref $value) {
            when ('ARRAY') { push @array, @$value[0 .. $#$value > 1 ? 1 : $#$value] }
            when ('HASH')  { $hash{tail}{52} = { %$value } }
            default       { $hash{tail_scalar}{52} = defined($value) ? qq{$value} : undef }
        }
    }
} else {
    $hash{missing_generated}{52} = sub { return q{missing} =~ s/i/I/r };
}
if (exists $hash{generated}{8}) {
    my $tmp_53 = $hash{generated}{8};
    for my $key (sort keys %{ $tmp_53 }) {
        my $value = $tmp_53->{$key};
        given (ref $value) {
            when ('ARRAY') { push @array, @$value[0 .. $#$value > 1 ? 1 : $#$value] }
            when ('HASH')  { $hash{tail}{53} = { %$value } }
            default       { $hash{tail_scalar}{53} = defined($value) ? qq{$value} : undef }
        }
    }
} else {
    $hash{missing_generated}{53} = sub { return q{missing} =~ s/i/I/r };
}
