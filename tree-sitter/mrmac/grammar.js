/// <reference types="tree-sitter-cli/dsl" />
// @ts-check

const PREC = {
  OR: 1,
  AND: 2,
  COMPARE: 3,
  SHIFT: 4,
  ADD: 5,
  MULTIPLY: 6,
  UNARY: 7,
};

function caseInsensitive(text) {
  return text
    .split('')
    .map((ch) => {
      if (/[a-zA-Z]/.test(ch)) return `[${ch.toLowerCase()}${ch.toUpperCase()}]`;
      if ('\\^$.|?*+()[]{}'.includes(ch)) return `\\${ch}`;
      return ch;
    })
    .join('');
}

function keyword(text) {
  return token(new RegExp(caseInsensitive(text)));
}

function sep1(rule, separator) {
  return seq(rule, repeat(seq(separator, rule)));
}

module.exports = grammar({
  name: 'mrmac',

  extras: $ => [
    /\s+/,
    $.comment,
  ],

  externals: $ => [
    $.comment,
  ],

  word: $ => $.identifier,

  rules: {
    source_file: $ => seq(
      optional($.byte_order_mark),
      repeat(choice(
        $.macro_file_definition,
        $.macro_definition,
      )),
    ),

    byte_order_mark: _ => token('\uFEFF'),

    macro_file_definition: $ => seq(
      alias($._smacro_file, $.directive),
      field('name', $.identifier),
      ';',
    ),

    macro_definition: $ => seq(
      alias($._smacro, $.directive),
      field('name', $.identifier),
      repeat($.macro_header_item),
      ';',
      repeat($.statement),
      alias($._end_macro, $.keyword),
      ';',
    ),

    macro_header_item: $ => choice(
      seq(alias($._to, $.keyword), field('binding', $.keyspec)),
      seq(alias($._from, $.keyword), field('mode', $.identifier)),
      alias($._trans, $.keyword),
      alias($._dump, $.keyword),
      alias($._perm, $.keyword),
    ),

    statement: $ => choice(
      seq($.call_statement, ';'),
      seq($.goto_statement, ';'),
      seq($.ret_statement, ';'),
      seq($.assignment_statement, ';'),
      seq($.if_statement, ';'),
      seq($.while_statement, ';'),
      seq($.variable_declaration, ';'),
      $.label_definition,
    ),

    variable_declaration: $ => seq(
      choice(
        alias($._def_int, $.keyword),
        alias($._def_str, $.keyword),
        alias($._def_char, $.keyword),
        alias($._def_real, $.keyword),
      ),
      '(',
      sep1($.identifier, ','),
      ')',
    ),

    label_definition: $ => seq(
      field('label', $.identifier),
      ':',
    ),

    goto_statement: $ => seq(
      alias($._goto, $.keyword),
      field('label', $.identifier),
    ),

    ret_statement: $ => alias($._ret, $.keyword),

    assignment_statement: $ => seq(
      field('target', $.identifier),
      ':=',
      field('value', $.expression),
    ),

    call_statement: $ => choice(
      seq(
        alias($._call, $.keyword),
        field('label', $.identifier),
      ),
      seq(
        alias($._tvcall, $.keyword),
        field('name', $.identifier),
        '(',
        optional($.argument_list),
        ')',
      ),
    ),

    if_statement: $ => seq(
      alias($._if, $.keyword),
      $.expression,
      alias($._then, $.keyword),
      repeat($.statement),
      optional(seq(
        alias($._else, $.keyword),
        repeat($.statement),
      )),
      alias($._end, $.keyword),
    ),

    while_statement: $ => seq(
      alias($._while, $.keyword),
      $.expression,
      alias($._do, $.keyword),
      repeat($.statement),
      alias($._end, $.keyword),
    ),

    argument_list: $ => sep1($.argument, ','),

    argument: $ => choice(
      $.expression,
      $.string_literal,
    ),

    expression: $ => choice(
      $.integer_literal,
      $.identifier,
      seq('(', $.expression, ')'),
      prec.right(PREC.UNARY, seq('-', $.expression)),
      prec.right(PREC.UNARY, seq(alias($._not, $.keyword), $.expression)),
      prec.left(PREC.MULTIPLY, seq($.expression, choice('*', '/', alias($._mod, $.keyword)), $.expression)),
      prec.left(PREC.ADD, seq($.expression, choice('+', '-'), $.expression)),
      prec.left(PREC.SHIFT, seq($.expression, choice(alias($._shl, $.keyword), alias($._shr, $.keyword)), $.expression)),
      prec.left(PREC.COMPARE, seq($.expression, choice('=', '<>', '<=', '>=', '<', '>'), $.expression)),
      prec.left(PREC.AND, seq($.expression, alias($._and, $.keyword), $.expression)),
      prec.left(PREC.OR, seq($.expression, alias($._or, $.keyword), $.expression)),
    ),

    identifier: _ => /[A-Za-z_][A-Za-z_0-9]*/,

    integer_literal: _ => token(choice(
      /\$[0-9A-Fa-f]+/,
      /[0-9]+/,
    )),

    string_literal: _ => token(seq(
      '\'',
      repeat(choice(
        /[^'\n]/,
        '\'\'',
      )),
      '\'',
    )),

    keyspec: _ => token(seq(
      '<',
      /[^>\n]+/,
      '>',
    )),

    _smacro: _ => token(new RegExp(`\\$${caseInsensitive('MACRO')}`)),
    _smacro_file: _ => keyword('SMACRO_FILE'),
    _end_macro: _ => keyword('END_MACRO'),
    _def_int: _ => keyword('DEF_INT'),
    _def_str: _ => keyword('DEF_STR'),
    _def_char: _ => keyword('DEF_CHAR'),
    _def_real: _ => keyword('DEF_REAL'),
    _if: _ => keyword('IF'),
    _then: _ => keyword('THEN'),
    _else: _ => keyword('ELSE'),
    _end: _ => keyword('END'),
    _while: _ => keyword('WHILE'),
    _do: _ => keyword('DO'),
    _tvcall: _ => keyword('TVCALL'),
    _call: _ => keyword('CALL'),
    _ret: _ => keyword('RET'),
    _goto: _ => keyword('GOTO'),
    _to: _ => keyword('TO'),
    _from: _ => keyword('FROM'),
    _trans: _ => keyword('TRANS'),
    _dump: _ => keyword('DUMP'),
    _perm: _ => keyword('PERM'),
    _and: _ => keyword('AND'),
    _or: _ => keyword('OR'),
    _not: _ => keyword('NOT'),
    _shl: _ => keyword('SHL'),
    _shr: _ => keyword('SHR'),
    _mod: _ => keyword('MOD'),
  },
});
