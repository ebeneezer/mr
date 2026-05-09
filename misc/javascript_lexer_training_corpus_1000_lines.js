#!/usr/bin/env node
// javascript_lexer_training_corpus_1000_lines.js
// Synthetic JavaScript lexer-training corpus.
// Purpose: broad lexical/syntactic coverage, dense nesting, many delimiters.
// It is intentionally not meant to be executed as production code.
/* block comment with tokens: if (x) { return /regex/g; } */
'use strict';
import defaultExport from "module-default";
import * as namespaceImport from "module-namespace";
import { named as renamed, other, default as aliasDefault } from "module-named";
import "side-effect-module";
export { renamed, other };
export default function exportedDefault(value = null) { return value ?? "default"; }
export const exportedConst = 123_456;
export async function exportedAsync() { return await Promise.resolve("ok"); }
export class ExportedClass {}
const moduleMeta = import.meta.url;
const dynamicModulePromise = import("dynamic-module");
var legacyVar = 1;
let mutableLet = 2;
const immutableConst = 3;
globalThis.__lexerTraining = true;
const numericLiterals = [
    0, -0, +0, 1, -1, 1.5, .5, 5., 1e9, 1E-9,
    0b1010_1010, 0o755, 0xDEAD_BEEF, 123_456_789,
    1n, 0b101n, 0o77n, 0xFFn,
    NaN, Infinity, -Infinity,
];
const stringLiterals = [
    "double quoted\nstring\twith escapes \x41 \u0042 \u{1F600}",
    'single quoted with \\\' quote and \\\\ slash',
    `template literal ${legacyVar + mutableLet} with ${(() => "expr")()}`,
    String.raw`raw\nnot newline ${"but expression still works"}`,
];
const regexLiterals = [
    /plain/,
    /[A-Z_$][\w$]*/g,
    /(?<name>\p{Letter}+)/giu,
    /(?<=prefix)\w+(?!suffix)/dmsuy,
    /\/\*.*?\*\//s,
    /#?[0-9a-f]{3,8}/iv,
];
const objectLiteral = {
    shorthand: legacyVar,
    mutableLet,
    "quoted-key": true,
    123: "numeric key",
    [`computed-${mutableLet}`]: "computed",
    get value() { return this.mutableLet ?? 0; },
    set value(v) { this.mutableLet = v; },
    method(a, b = 1, ...rest) { return [a, b, rest]; },
    async asyncMethod() { return await Promise.resolve(1); },
    *generatorMethod() { yield* [1, 2, 3]; },
    async *asyncGeneratorMethod() { yield await Promise.resolve(4); },
};
const arrayLiteral = [
    1,
    ,
    3,
    ...[4, 5],
    function inlineFunction(x) { return x * x; },
    async x => await x,
    function* inlineGenerator() { yield "inside"; },
];
let [firstItem, , thirdItem, ...remainingItems] = arrayLiteral;
let { shorthand, ["quoted-key"]: quotedKey, missing = "fallback", ...objectRest } = objectLiteral;
let nestedDestructuring = { a: { b: [{ c: 42 }] } };
let { a: { b: [ { c: deeplyNested = 0 } ] } } = nestedDestructuring;
function normalFunction(a, b = 2, ...rest) {
    arguments.length;
    return { a, b, rest, thisValue: this };
}
function* generatorFunction(limit = 3) {
    yield "start";
    for (let i = 0; i < limit; i++) {
        yield i;
    }
    yield* ["end-a", "end-b"];
    return "done";
}
async function asyncFunction(input) {
    try {
        const value = await Promise.resolve(input);
        return value?.nested?.field ?? "missing";
    } catch (error) {
        throw new Error(`async failure: ${error?.message}`);
    } finally {
        mutableLet++;
    }
}
async function* asyncGeneratorFunction(items) {
    for await (const item of items) {
        yield await Promise.resolve(item);
    }
}
const arrowOne = x => x + 1;
const arrowTwo = (x, y = 2, ...rest) => ({ x, y, rest });
const arrowBlock = ({ x, y }) => {
    let sum = x + y;
    return sum ** 2;
};
class BaseClass {
    constructor(name = "base") {
        this.name = name;
    }
    speak() {
        return `Base:${this.name}`;
    }
    static baseStatic() {
        return "base-static";
    }
}
class DerivedClass extends BaseClass {
    #privateField = 10;
    static staticField = "static";
    publicField = "public";
    ["computedMethod"]() { return this.#privateField; }
    static {
        this.initialized = true;
    }
    constructor(name, options = {}) {
        super(name);
        this.options = { ...options };
    }
    get secret() {
        return this.#privateField;
    }
    set secret(value) {
        this.#privateField = Number(value);
    }
    async run(task) {
        return await task?.();
    }
    *iterate() {
        yield this.name;
        yield this.publicField;
    }
}
const derived = new DerivedClass("demo", { flag: true });
derived.secret ||= 11;
derived.options.enabled &&= true;
derived.options.title ??= "untitled";
const optionalCall = derived.missing?.method?.(1, 2) ?? null;
labelOuter:
for (let i = 0; i < 5; i++) {
    labelInner:
    for (let j = 0; j < 5; j++) {
        if (i === j) continue labelInner;
        if (i * j > 8) break labelOuter;
        mutableLet += i + j;
    }
}
let whileCounter = 0;
while (whileCounter < 3) {
    whileCounter++;
}
do {
    whileCounter--;
} while (whileCounter > 0);
for (const key in objectLiteral) {
    if (!Object.hasOwn(objectLiteral, key)) continue;
    mutableLet += key.length;
}
for (const value of [1, 2, 3]) {
    mutableLet += value;
}
switch (mutableLet % 4) {
    case 0:
        mutableLet += 10;
        break;
    case 1:
    case 2:
        mutableLet += 20;
        break;
    default:
        mutableLet += 30;
}
try {
    if (mutableLet < 0) {
        throw new RangeError("negative mutableLet");
    }
} catch (error) {
    console.error(error instanceof Error ? error.message : error);
} finally {
    mutableLet = Math.max(0, mutableLet);
}
const operatorSoup = (((1 + 2) * 3 - 4) / 5) % 6;
let bitSoup = 0b1010;
bitSoup <<= 1;
bitSoup >>= 1;
bitSoup >>>= 1;
bitSoup &= 0xff;
bitSoup |= 0x10;
bitSoup ^= 0x01;
let logicalSoup = true && false || !false;
logicalSoup &&= Boolean(bitSoup);
logicalSoup ||= false;
logicalSoup ??= true;
const comparisonSoup = [
    1 == "1",
    1 === Number("1"),
    1 != "2",
    1 !== "1",
    1 < 2,
    2 <= 2,
    3 > 2,
    3 >= 3,
    "a" in objectLiteral,
    derived instanceof DerivedClass,
];
const unarySoup = [
    typeof mutableLet,
    void mutableLet,
    delete objectLiteral.shorthand,
    +mutableLet,
    -mutableLet,
    ~mutableLet,
    !mutableLet,
];
const tagged = (strings, ...values) => ({ strings, values });
const taggedResult = tagged`alpha ${1 + 2} beta ${derived?.name ?? "none"} gamma`;
const map = new Map([
    ["one", 1],
    ["two", 2],
]);
const set = new Set([1, 2, 3]);
const weakMap = new WeakMap();
const weakSet = new WeakSet();
const symbolKey = Symbol.for("lexer.symbol");
const bigIntMath = (10n ** 3n) / 2n;
const promiseChain = Promise.resolve(1)
    .then(value => value + 1)
    .catch(error => { throw error; })
    .finally(() => "finally");
const jsonLike = {
    nullValue: null,
    trueValue: true,
    falseValue: false,
    undefinedValue: undefined,
};
const asiTrapOne = legacyVar
++mutableLet;
const asiTrapTwo = legacyVar
/regexAfterLineBreak/.test("regexAfterLineBreak");
with ({ legacyVar: 99 }) {
    legacyVar;
}
const generated_001 = {
    id: 1,
    name: `generated-${1}-${mutableLet}`,
    rx: /^(?<prefix>g001)-(?<value>\d+)$/gim,
    data: [
        { index: 0, text: "zero", flags: [true, false, null] },
        { index: 1, text: 'one', flags: [false, true, undefined] },
        { index: 2, text: `two-${1}`, flags: [Boolean(1), !1] },
    ],
    method(input = {}) {
        const local = { ...input, id: 1 };
        for (let outer = 0; outer < 3; outer++) {
            for (let inner = 0; inner < 3; inner++) {
                if ((outer + inner + 1) % 2 === 0) {
                    local[`even_${outer}_${inner}`] = outer ** inner;
                } else if (inner === 1) {
                    local[`middle_${outer}`] = /middle/.test("middle");
                } else {
                    local[`odd_${outer}_${inner}`] = `${outer}:${inner}:${1}`;
                }
            }
        }
        return local;
    },
    async *stream(items = []) {
        for await (const item of items) {
            yield { item, block: 1, optional: item?.value ?? null };
        }
    },
};
const generatedResult_001 = generated_001.method({ seed: 1, nested: { a: [1, 2, 3] } });
const generated_002 = {
    id: 2,
    name: `generated-${2}-${mutableLet}`,
    rx: /^(?<prefix>g002)-(?<value>\d+)$/gim,
    data: [
        { index: 0, text: "zero", flags: [true, false, null] },
        { index: 1, text: 'one', flags: [false, true, undefined] },
        { index: 2, text: `two-${2}`, flags: [Boolean(2), !2] },
    ],
    method(input = {}) {
        const local = { ...input, id: 2 };
        for (let outer = 0; outer < 3; outer++) {
            for (let inner = 0; inner < 3; inner++) {
                if ((outer + inner + 2) % 2 === 0) {
                    local[`even_${outer}_${inner}`] = outer ** inner;
                } else if (inner === 1) {
                    local[`middle_${outer}`] = /middle/.test("middle");
                } else {
                    local[`odd_${outer}_${inner}`] = `${outer}:${inner}:${2}`;
                }
            }
        }
        return local;
    },
    async *stream(items = []) {
        for await (const item of items) {
            yield { item, block: 2, optional: item?.value ?? null };
        }
    },
};
const generatedResult_002 = generated_002.method({ seed: 2, nested: { a: [1, 2, 3] } });
const generated_003 = {
    id: 3,
    name: `generated-${3}-${mutableLet}`,
    rx: /^(?<prefix>g003)-(?<value>\d+)$/gim,
    data: [
        { index: 0, text: "zero", flags: [true, false, null] },
        { index: 1, text: 'one', flags: [false, true, undefined] },
        { index: 2, text: `two-${3}`, flags: [Boolean(3), !3] },
    ],
    method(input = {}) {
        const local = { ...input, id: 3 };
        for (let outer = 0; outer < 3; outer++) {
            for (let inner = 0; inner < 3; inner++) {
                if ((outer + inner + 3) % 2 === 0) {
                    local[`even_${outer}_${inner}`] = outer ** inner;
                } else if (inner === 1) {
                    local[`middle_${outer}`] = /middle/.test("middle");
                } else {
                    local[`odd_${outer}_${inner}`] = `${outer}:${inner}:${3}`;
                }
            }
        }
        return local;
    },
    async *stream(items = []) {
        for await (const item of items) {
            yield { item, block: 3, optional: item?.value ?? null };
        }
    },
};
const generatedResult_003 = generated_003.method({ seed: 3, nested: { a: [1, 2, 3] } });
const generated_004 = {
    id: 4,
    name: `generated-${4}-${mutableLet}`,
    rx: /^(?<prefix>g004)-(?<value>\d+)$/gim,
    data: [
        { index: 0, text: "zero", flags: [true, false, null] },
        { index: 1, text: 'one', flags: [false, true, undefined] },
        { index: 2, text: `two-${4}`, flags: [Boolean(4), !4] },
    ],
    method(input = {}) {
        const local = { ...input, id: 4 };
        for (let outer = 0; outer < 3; outer++) {
            for (let inner = 0; inner < 3; inner++) {
                if ((outer + inner + 4) % 2 === 0) {
                    local[`even_${outer}_${inner}`] = outer ** inner;
                } else if (inner === 1) {
                    local[`middle_${outer}`] = /middle/.test("middle");
                } else {
                    local[`odd_${outer}_${inner}`] = `${outer}:${inner}:${4}`;
                }
            }
        }
        return local;
    },
    async *stream(items = []) {
        for await (const item of items) {
            yield { item, block: 4, optional: item?.value ?? null };
        }
    },
};
const generatedResult_004 = generated_004.method({ seed: 4, nested: { a: [1, 2, 3] } });
const generated_005 = {
    id: 5,
    name: `generated-${5}-${mutableLet}`,
    rx: /^(?<prefix>g005)-(?<value>\d+)$/gim,
    data: [
        { index: 0, text: "zero", flags: [true, false, null] },
        { index: 1, text: 'one', flags: [false, true, undefined] },
        { index: 2, text: `two-${5}`, flags: [Boolean(5), !5] },
    ],
    method(input = {}) {
        const local = { ...input, id: 5 };
        for (let outer = 0; outer < 3; outer++) {
            for (let inner = 0; inner < 3; inner++) {
                if ((outer + inner + 5) % 2 === 0) {
                    local[`even_${outer}_${inner}`] = outer ** inner;
                } else if (inner === 1) {
                    local[`middle_${outer}`] = /middle/.test("middle");
                } else {
                    local[`odd_${outer}_${inner}`] = `${outer}:${inner}:${5}`;
                }
            }
        }
        return local;
    },
    async *stream(items = []) {
        for await (const item of items) {
            yield { item, block: 5, optional: item?.value ?? null };
        }
    },
};
const generatedResult_005 = generated_005.method({ seed: 5, nested: { a: [1, 2, 3] } });
const generated_006 = {
    id: 6,
    name: `generated-${6}-${mutableLet}`,
    rx: /^(?<prefix>g006)-(?<value>\d+)$/gim,
    data: [
        { index: 0, text: "zero", flags: [true, false, null] },
        { index: 1, text: 'one', flags: [false, true, undefined] },
        { index: 2, text: `two-${6}`, flags: [Boolean(6), !6] },
    ],
    method(input = {}) {
        const local = { ...input, id: 6 };
        for (let outer = 0; outer < 3; outer++) {
            for (let inner = 0; inner < 3; inner++) {
                if ((outer + inner + 6) % 2 === 0) {
                    local[`even_${outer}_${inner}`] = outer ** inner;
                } else if (inner === 1) {
                    local[`middle_${outer}`] = /middle/.test("middle");
                } else {
                    local[`odd_${outer}_${inner}`] = `${outer}:${inner}:${6}`;
                }
            }
        }
        return local;
    },
    async *stream(items = []) {
        for await (const item of items) {
            yield { item, block: 6, optional: item?.value ?? null };
        }
    },
};
const generatedResult_006 = generated_006.method({ seed: 6, nested: { a: [1, 2, 3] } });
const generated_007 = {
    id: 7,
    name: `generated-${7}-${mutableLet}`,
    rx: /^(?<prefix>g007)-(?<value>\d+)$/gim,
    data: [
        { index: 0, text: "zero", flags: [true, false, null] },
        { index: 1, text: 'one', flags: [false, true, undefined] },
        { index: 2, text: `two-${7}`, flags: [Boolean(7), !7] },
    ],
    method(input = {}) {
        const local = { ...input, id: 7 };
        for (let outer = 0; outer < 3; outer++) {
            for (let inner = 0; inner < 3; inner++) {
                if ((outer + inner + 7) % 2 === 0) {
                    local[`even_${outer}_${inner}`] = outer ** inner;
                } else if (inner === 1) {
                    local[`middle_${outer}`] = /middle/.test("middle");
                } else {
                    local[`odd_${outer}_${inner}`] = `${outer}:${inner}:${7}`;
                }
            }
        }
        return local;
    },
    async *stream(items = []) {
        for await (const item of items) {
            yield { item, block: 7, optional: item?.value ?? null };
        }
    },
};
const generatedResult_007 = generated_007.method({ seed: 7, nested: { a: [1, 2, 3] } });
const generated_008 = {
    id: 8,
    name: `generated-${8}-${mutableLet}`,
    rx: /^(?<prefix>g008)-(?<value>\d+)$/gim,
    data: [
        { index: 0, text: "zero", flags: [true, false, null] },
        { index: 1, text: 'one', flags: [false, true, undefined] },
        { index: 2, text: `two-${8}`, flags: [Boolean(8), !8] },
    ],
    method(input = {}) {
        const local = { ...input, id: 8 };
        for (let outer = 0; outer < 3; outer++) {
            for (let inner = 0; inner < 3; inner++) {
                if ((outer + inner + 8) % 2 === 0) {
                    local[`even_${outer}_${inner}`] = outer ** inner;
                } else if (inner === 1) {
                    local[`middle_${outer}`] = /middle/.test("middle");
                } else {
                    local[`odd_${outer}_${inner}`] = `${outer}:${inner}:${8}`;
                }
            }
        }
        return local;
    },
    async *stream(items = []) {
        for await (const item of items) {
            yield { item, block: 8, optional: item?.value ?? null };
        }
    },
};
const generatedResult_008 = generated_008.method({ seed: 8, nested: { a: [1, 2, 3] } });
const generated_009 = {
    id: 9,
    name: `generated-${9}-${mutableLet}`,
    rx: /^(?<prefix>g009)-(?<value>\d+)$/gim,
    data: [
        { index: 0, text: "zero", flags: [true, false, null] },
        { index: 1, text: 'one', flags: [false, true, undefined] },
        { index: 2, text: `two-${9}`, flags: [Boolean(9), !9] },
    ],
    method(input = {}) {
        const local = { ...input, id: 9 };
        for (let outer = 0; outer < 3; outer++) {
            for (let inner = 0; inner < 3; inner++) {
                if ((outer + inner + 9) % 2 === 0) {
                    local[`even_${outer}_${inner}`] = outer ** inner;
                } else if (inner === 1) {
                    local[`middle_${outer}`] = /middle/.test("middle");
                } else {
                    local[`odd_${outer}_${inner}`] = `${outer}:${inner}:${9}`;
                }
            }
        }
        return local;
    },
    async *stream(items = []) {
        for await (const item of items) {
            yield { item, block: 9, optional: item?.value ?? null };
        }
    },
};
const generatedResult_009 = generated_009.method({ seed: 9, nested: { a: [1, 2, 3] } });
const generated_010 = {
    id: 10,
    name: `generated-${10}-${mutableLet}`,
    rx: /^(?<prefix>g010)-(?<value>\d+)$/gim,
    data: [
        { index: 0, text: "zero", flags: [true, false, null] },
        { index: 1, text: 'one', flags: [false, true, undefined] },
        { index: 2, text: `two-${10}`, flags: [Boolean(10), !10] },
    ],
    method(input = {}) {
        const local = { ...input, id: 10 };
        for (let outer = 0; outer < 3; outer++) {
            for (let inner = 0; inner < 3; inner++) {
                if ((outer + inner + 10) % 2 === 0) {
                    local[`even_${outer}_${inner}`] = outer ** inner;
                } else if (inner === 1) {
                    local[`middle_${outer}`] = /middle/.test("middle");
                } else {
                    local[`odd_${outer}_${inner}`] = `${outer}:${inner}:${10}`;
                }
            }
        }
        return local;
    },
    async *stream(items = []) {
        for await (const item of items) {
            yield { item, block: 10, optional: item?.value ?? null };
        }
    },
};
const generatedResult_010 = generated_010.method({ seed: 10, nested: { a: [1, 2, 3] } });
const generated_011 = {
    id: 11,
    name: `generated-${11}-${mutableLet}`,
    rx: /^(?<prefix>g011)-(?<value>\d+)$/gim,
    data: [
        { index: 0, text: "zero", flags: [true, false, null] },
        { index: 1, text: 'one', flags: [false, true, undefined] },
        { index: 2, text: `two-${11}`, flags: [Boolean(11), !11] },
    ],
    method(input = {}) {
        const local = { ...input, id: 11 };
        for (let outer = 0; outer < 3; outer++) {
            for (let inner = 0; inner < 3; inner++) {
                if ((outer + inner + 11) % 2 === 0) {
                    local[`even_${outer}_${inner}`] = outer ** inner;
                } else if (inner === 1) {
                    local[`middle_${outer}`] = /middle/.test("middle");
                } else {
                    local[`odd_${outer}_${inner}`] = `${outer}:${inner}:${11}`;
                }
            }
        }
        return local;
    },
    async *stream(items = []) {
        for await (const item of items) {
            yield { item, block: 11, optional: item?.value ?? null };
        }
    },
};
const generatedResult_011 = generated_011.method({ seed: 11, nested: { a: [1, 2, 3] } });
const generated_012 = {
    id: 12,
    name: `generated-${12}-${mutableLet}`,
    rx: /^(?<prefix>g012)-(?<value>\d+)$/gim,
    data: [
        { index: 0, text: "zero", flags: [true, false, null] },
        { index: 1, text: 'one', flags: [false, true, undefined] },
        { index: 2, text: `two-${12}`, flags: [Boolean(12), !12] },
    ],
    method(input = {}) {
        const local = { ...input, id: 12 };
        for (let outer = 0; outer < 3; outer++) {
            for (let inner = 0; inner < 3; inner++) {
                if ((outer + inner + 12) % 2 === 0) {
                    local[`even_${outer}_${inner}`] = outer ** inner;
                } else if (inner === 1) {
                    local[`middle_${outer}`] = /middle/.test("middle");
                } else {
                    local[`odd_${outer}_${inner}`] = `${outer}:${inner}:${12}`;
                }
            }
        }
        return local;
    },
    async *stream(items = []) {
        for await (const item of items) {
            yield { item, block: 12, optional: item?.value ?? null };
        }
    },
};
const generatedResult_012 = generated_012.method({ seed: 12, nested: { a: [1, 2, 3] } });
const generated_013 = {
    id: 13,
    name: `generated-${13}-${mutableLet}`,
    rx: /^(?<prefix>g013)-(?<value>\d+)$/gim,
    data: [
        { index: 0, text: "zero", flags: [true, false, null] },
        { index: 1, text: 'one', flags: [false, true, undefined] },
        { index: 2, text: `two-${13}`, flags: [Boolean(13), !13] },
    ],
    method(input = {}) {
        const local = { ...input, id: 13 };
        for (let outer = 0; outer < 3; outer++) {
            for (let inner = 0; inner < 3; inner++) {
                if ((outer + inner + 13) % 2 === 0) {
                    local[`even_${outer}_${inner}`] = outer ** inner;
                } else if (inner === 1) {
                    local[`middle_${outer}`] = /middle/.test("middle");
                } else {
                    local[`odd_${outer}_${inner}`] = `${outer}:${inner}:${13}`;
                }
            }
        }
        return local;
    },
    async *stream(items = []) {
        for await (const item of items) {
            yield { item, block: 13, optional: item?.value ?? null };
        }
    },
};
const generatedResult_013 = generated_013.method({ seed: 13, nested: { a: [1, 2, 3] } });
const generated_014 = {
    id: 14,
    name: `generated-${14}-${mutableLet}`,
    rx: /^(?<prefix>g014)-(?<value>\d+)$/gim,
    data: [
        { index: 0, text: "zero", flags: [true, false, null] },
        { index: 1, text: 'one', flags: [false, true, undefined] },
        { index: 2, text: `two-${14}`, flags: [Boolean(14), !14] },
    ],
    method(input = {}) {
        const local = { ...input, id: 14 };
        for (let outer = 0; outer < 3; outer++) {
            for (let inner = 0; inner < 3; inner++) {
                if ((outer + inner + 14) % 2 === 0) {
                    local[`even_${outer}_${inner}`] = outer ** inner;
                } else if (inner === 1) {
                    local[`middle_${outer}`] = /middle/.test("middle");
                } else {
                    local[`odd_${outer}_${inner}`] = `${outer}:${inner}:${14}`;
                }
            }
        }
        return local;
    },
    async *stream(items = []) {
        for await (const item of items) {
            yield { item, block: 14, optional: item?.value ?? null };
        }
    },
};
const generatedResult_014 = generated_014.method({ seed: 14, nested: { a: [1, 2, 3] } });
const generated_015 = {
    id: 15,
    name: `generated-${15}-${mutableLet}`,
    rx: /^(?<prefix>g015)-(?<value>\d+)$/gim,
    data: [
        { index: 0, text: "zero", flags: [true, false, null] },
        { index: 1, text: 'one', flags: [false, true, undefined] },
        { index: 2, text: `two-${15}`, flags: [Boolean(15), !15] },
    ],
    method(input = {}) {
        const local = { ...input, id: 15 };
        for (let outer = 0; outer < 3; outer++) {
            for (let inner = 0; inner < 3; inner++) {
                if ((outer + inner + 15) % 2 === 0) {
                    local[`even_${outer}_${inner}`] = outer ** inner;
                } else if (inner === 1) {
                    local[`middle_${outer}`] = /middle/.test("middle");
                } else {
                    local[`odd_${outer}_${inner}`] = `${outer}:${inner}:${15}`;
                }
            }
        }
        return local;
    },
    async *stream(items = []) {
        for await (const item of items) {
            yield { item, block: 15, optional: item?.value ?? null };
        }
    },
};
const generatedResult_015 = generated_015.method({ seed: 15, nested: { a: [1, 2, 3] } });
const generated_016 = {
    id: 16,
    name: `generated-${16}-${mutableLet}`,
    rx: /^(?<prefix>g016)-(?<value>\d+)$/gim,
    data: [
        { index: 0, text: "zero", flags: [true, false, null] },
        { index: 1, text: 'one', flags: [false, true, undefined] },
        { index: 2, text: `two-${16}`, flags: [Boolean(16), !16] },
    ],
    method(input = {}) {
        const local = { ...input, id: 16 };
        for (let outer = 0; outer < 3; outer++) {
            for (let inner = 0; inner < 3; inner++) {
                if ((outer + inner + 16) % 2 === 0) {
                    local[`even_${outer}_${inner}`] = outer ** inner;
                } else if (inner === 1) {
                    local[`middle_${outer}`] = /middle/.test("middle");
                } else {
                    local[`odd_${outer}_${inner}`] = `${outer}:${inner}:${16}`;
                }
            }
        }
        return local;
    },
    async *stream(items = []) {
        for await (const item of items) {
            yield { item, block: 16, optional: item?.value ?? null };
        }
    },
};
const generatedResult_016 = generated_016.method({ seed: 16, nested: { a: [1, 2, 3] } });
const generated_017 = {
    id: 17,
    name: `generated-${17}-${mutableLet}`,
    rx: /^(?<prefix>g017)-(?<value>\d+)$/gim,
    data: [
        { index: 0, text: "zero", flags: [true, false, null] },
        { index: 1, text: 'one', flags: [false, true, undefined] },
        { index: 2, text: `two-${17}`, flags: [Boolean(17), !17] },
    ],
    method(input = {}) {
        const local = { ...input, id: 17 };
        for (let outer = 0; outer < 3; outer++) {
            for (let inner = 0; inner < 3; inner++) {
                if ((outer + inner + 17) % 2 === 0) {
                    local[`even_${outer}_${inner}`] = outer ** inner;
                } else if (inner === 1) {
                    local[`middle_${outer}`] = /middle/.test("middle");
                } else {
                    local[`odd_${outer}_${inner}`] = `${outer}:${inner}:${17}`;
                }
            }
        }
        return local;
    },
    async *stream(items = []) {
        for await (const item of items) {
            yield { item, block: 17, optional: item?.value ?? null };
        }
    },
};
const generatedResult_017 = generated_017.method({ seed: 17, nested: { a: [1, 2, 3] } });
const generated_018 = {
    id: 18,
    name: `generated-${18}-${mutableLet}`,
    rx: /^(?<prefix>g018)-(?<value>\d+)$/gim,
    data: [
        { index: 0, text: "zero", flags: [true, false, null] },
        { index: 1, text: 'one', flags: [false, true, undefined] },
        { index: 2, text: `two-${18}`, flags: [Boolean(18), !18] },
    ],
    method(input = {}) {
        const local = { ...input, id: 18 };
        for (let outer = 0; outer < 3; outer++) {
            for (let inner = 0; inner < 3; inner++) {
                if ((outer + inner + 18) % 2 === 0) {
                    local[`even_${outer}_${inner}`] = outer ** inner;
                } else if (inner === 1) {
                    local[`middle_${outer}`] = /middle/.test("middle");
                } else {
                    local[`odd_${outer}_${inner}`] = `${outer}:${inner}:${18}`;
                }
            }
        }
        return local;
    },
    async *stream(items = []) {
        for await (const item of items) {
            yield { item, block: 18, optional: item?.value ?? null };
        }
    },
};
const generatedResult_018 = generated_018.method({ seed: 18, nested: { a: [1, 2, 3] } });
const generated_019 = {
    id: 19,
    name: `generated-${19}-${mutableLet}`,
    rx: /^(?<prefix>g019)-(?<value>\d+)$/gim,
    data: [
        { index: 0, text: "zero", flags: [true, false, null] },
        { index: 1, text: 'one', flags: [false, true, undefined] },
        { index: 2, text: `two-${19}`, flags: [Boolean(19), !19] },
    ],
    method(input = {}) {
        const local = { ...input, id: 19 };
        for (let outer = 0; outer < 3; outer++) {
            for (let inner = 0; inner < 3; inner++) {
                if ((outer + inner + 19) % 2 === 0) {
                    local[`even_${outer}_${inner}`] = outer ** inner;
                } else if (inner === 1) {
                    local[`middle_${outer}`] = /middle/.test("middle");
                } else {
                    local[`odd_${outer}_${inner}`] = `${outer}:${inner}:${19}`;
                }
            }
        }
        return local;
    },
    async *stream(items = []) {
        for await (const item of items) {
            yield { item, block: 19, optional: item?.value ?? null };
        }
    },
};
const generatedResult_019 = generated_019.method({ seed: 19, nested: { a: [1, 2, 3] } });
const generated_020 = {
    id: 20,
    name: `generated-${20}-${mutableLet}`,
    rx: /^(?<prefix>g020)-(?<value>\d+)$/gim,
    data: [
        { index: 0, text: "zero", flags: [true, false, null] },
        { index: 1, text: 'one', flags: [false, true, undefined] },
        { index: 2, text: `two-${20}`, flags: [Boolean(20), !20] },
    ],
    method(input = {}) {
        const local = { ...input, id: 20 };
        for (let outer = 0; outer < 3; outer++) {
            for (let inner = 0; inner < 3; inner++) {
                if ((outer + inner + 20) % 2 === 0) {
                    local[`even_${outer}_${inner}`] = outer ** inner;
                } else if (inner === 1) {
                    local[`middle_${outer}`] = /middle/.test("middle");
                } else {
                    local[`odd_${outer}_${inner}`] = `${outer}:${inner}:${20}`;
                }
            }
        }
        return local;
    },
    async *stream(items = []) {
        for await (const item of items) {
            yield { item, block: 20, optional: item?.value ?? null };
        }
    },
};
const generatedResult_020 = generated_020.method({ seed: 20, nested: { a: [1, 2, 3] } });
const generated_021 = {
    id: 21,
    name: `generated-${21}-${mutableLet}`,
    rx: /^(?<prefix>g021)-(?<value>\d+)$/gim,
    data: [
        { index: 0, text: "zero", flags: [true, false, null] },
        { index: 1, text: 'one', flags: [false, true, undefined] },
        { index: 2, text: `two-${21}`, flags: [Boolean(21), !21] },
    ],
    method(input = {}) {
        const local = { ...input, id: 21 };
        for (let outer = 0; outer < 3; outer++) {
            for (let inner = 0; inner < 3; inner++) {
                if ((outer + inner + 21) % 2 === 0) {
                    local[`even_${outer}_${inner}`] = outer ** inner;
                } else if (inner === 1) {
                    local[`middle_${outer}`] = /middle/.test("middle");
                } else {
                    local[`odd_${outer}_${inner}`] = `${outer}:${inner}:${21}`;
                }
            }
        }
        return local;
    },
    async *stream(items = []) {
        for await (const item of items) {
            yield { item, block: 21, optional: item?.value ?? null };
        }
    },
};
const generatedResult_021 = generated_021.method({ seed: 21, nested: { a: [1, 2, 3] } });
const generated_022 = {
    id: 22,
    name: `generated-${22}-${mutableLet}`,
    rx: /^(?<prefix>g022)-(?<value>\d+)$/gim,
    data: [
        { index: 0, text: "zero", flags: [true, false, null] },
        { index: 1, text: 'one', flags: [false, true, undefined] },
        { index: 2, text: `two-${22}`, flags: [Boolean(22), !22] },
    ],
    method(input = {}) {
        const local = { ...input, id: 22 };
        for (let outer = 0; outer < 3; outer++) {
            for (let inner = 0; inner < 3; inner++) {
                if ((outer + inner + 22) % 2 === 0) {
                    local[`even_${outer}_${inner}`] = outer ** inner;
                } else if (inner === 1) {
                    local[`middle_${outer}`] = /middle/.test("middle");
                } else {
                    local[`odd_${outer}_${inner}`] = `${outer}:${inner}:${22}`;
                }
            }
        }
        return local;
    },
    async *stream(items = []) {
        for await (const item of items) {
            yield { item, block: 22, optional: item?.value ?? null };
        }
    },
};
const generatedResult_022 = generated_022.method({ seed: 22, nested: { a: [1, 2, 3] } });
const generated_023 = {
    id: 23,
    name: `generated-${23}-${mutableLet}`,
    rx: /^(?<prefix>g023)-(?<value>\d+)$/gim,
    data: [
        { index: 0, text: "zero", flags: [true, false, null] },
        { index: 1, text: 'one', flags: [false, true, undefined] },
        { index: 2, text: `two-${23}`, flags: [Boolean(23), !23] },
    ],
    method(input = {}) {
        const local = { ...input, id: 23 };
        for (let outer = 0; outer < 3; outer++) {
            for (let inner = 0; inner < 3; inner++) {
                if ((outer + inner + 23) % 2 === 0) {
                    local[`even_${outer}_${inner}`] = outer ** inner;
                } else if (inner === 1) {
                    local[`middle_${outer}`] = /middle/.test("middle");
                } else {
                    local[`odd_${outer}_${inner}`] = `${outer}:${inner}:${23}`;
                }
            }
        }
        return local;
    },
    async *stream(items = []) {
        for await (const item of items) {
            yield { item, block: 23, optional: item?.value ?? null };
        }
    },
};
const generatedResult_023 = generated_023.method({ seed: 23, nested: { a: [1, 2, 3] } });
// More expression and statement edge cases.
const commaExpression = (legacyVar++, mutableLet++, immutableConst);
const conditionalExpression = mutableLet > 10 ? "large" : mutableLet > 5 ? "medium" : "small";
const nestedCalls = normalFunction(
    arrowOne(1),
    arrowTwo(2, 3, "x", "y").y,
    ...remainingItems,
);
const escapedIdentifiers = {
    \u0061: "unicode escape key",
    café: "unicode identifier",
    $dollar: "$",
    _underscore: "_",
};
function recursion(n) {
    if (n <= 1) return 1;
    return n * recursion(n - 1);
}
function defaultDestructuring(
    { a = 1, b: { c = 2 } = {}, ...rest } = {},
    [x, y = 2, ...tail] = []
) {
    return { a, c, rest, x, y, tail };
}
const nestedObject = {
    level1: {
        level2: {
            level3: {
                value: defaultDestructuring({ b: { c: 9 } }, [1, 2, 3]),
            },
        },
    },
};
const regexAmbiguityA = value => /abc/.test(String(value));
const divisionAmbiguity = operatorSoup / 2 / 3;
const regexWithSlash = /https?:\/\/[^/\s]+\/?/g;
const regexWithClass = /[/\\()[\]{}.*+?^$|]/g;
const multilineTemplate = `
line one
};
