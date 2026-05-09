#!/usr/bin/env swift
// swift_lexer_training_corpus_1000_lines.swift
// Synthetic Swift lexer-training corpus.
// Purpose: broad lexical/syntactic coverage, dense nesting, many delimiters.
// It is intentionally not meant to be compiled or executed as production code.
/*
    Block comment with tokens: if true { return /regex/ }
    Nested comment marker follows:
    /* nested block comment */
*/
import Foundation
#if canImport(Dispatch)
import Dispatch
#endif
#warning("Synthetic lexer corpus: warnings are intentional")
#if false
#error("Intentional inactive diagnostic branch")
#endif
@available(macOS 13.0, iOS 16.0, *)
public typealias Identifier = String
let decimalInteger = 123_456
let binaryInteger = 0b1010_0101
let octalInteger = 0o755
let hexInteger = 0xDEAD_BEEF
let decimalFloat = 123.456
let exponentFloat = 1.25e-8
let hexFloat = 0xFp-2
let boolTrue = true
let boolFalse = false
let nilValue: String? = nil
let simpleString = "double quoted string with escapes \n \t \" \\ \u{1F600}"
let rawString = #"raw string with \#(decimalInteger) interpolation and \n literal"#
let rawHashString = ##"two-hash raw string with ## delimiters \##(hexInteger)"##
let multilineString = """
line one
line two \(decimalInteger)
line three with "quotes"
"""
let rawMultilineString = #"""
raw multiline \#(binaryInteger)
slash \ backslash stays visible
"""#
let tupleValue = (name: "tuple", count: 3, nested: (x: 1, y: 2))
let arrayValue: [Any] = [1, "two", true, nil as String?, ["nested": 42]]
let dictionaryValue: [String: Any] = [
    "alpha": 1,
    "beta": [1, 2, 3],
    "gamma": ["nested": ["deep": true]],
]
let setValue: Set<String> = ["a", "b", "c"]
let rangeClosed = 1...5
let rangeHalfOpen = 1..<5
let partialFrom = 3...
let partialThrough = ...7
let partialUpTo = ..<9
precedencegroup LexerPrecedence {
    associativity: left
    higherThan: AdditionPrecedence
}
infix operator <+>: LexerPrecedence
prefix operator ±
postfix operator °
func <+> (lhs: Int, rhs: Int) -> Int { lhs + rhs + 1 }
prefix func ± (value: Int) -> Int { -value }
postfix func ° (value: Double) -> Double { value * .pi / 180.0 }
@propertyWrapper
struct Clamped<Value: Comparable> {
    private var value: Value
    let range: ClosedRange<Value>
    var wrappedValue: Value {
        get { value }
        set { value = min(max(newValue, range.lowerBound), range.upperBound) }
    }
    var projectedValue: ClosedRange<Value> { range }
    init(wrappedValue: Value, _ range: ClosedRange<Value>) {
        self.range = range
        self.value = min(max(wrappedValue, range.lowerBound), range.upperBound)
    }
}
@resultBuilder
enum TextBuilder {
    static func buildBlock(_ parts: String...) -> String { parts.joined(separator: "\n") }
    static func buildOptional(_ component: String?) -> String { component ?? "" }
    static func buildEither(first component: String) -> String { component }
    static func buildEither(second component: String) -> String { component }
    static func buildArray(_ components: [String]) -> String { components.joined(separator: ",") }
    static func buildExpression(_ expression: String) -> String { expression }
}
protocol Renderable: CustomStringConvertible {
    associatedtype Body
    var id: Identifier { get }
    @TextBuilder func render() -> String
    mutating func update(with body: Body) throws
}
protocol StorageProtocol {
    associatedtype Element: Hashable
    subscript(index: Int) -> Element { get set }
    func all() -> [Element]
}
enum LexerError: Error, CustomStringConvertible {
    case empty
    case invalid(token: String, line: Int, column: Int)
    case nested([LexerError])
    var description: String {
        switch self {
        case .empty:
            return "empty"
        case let .invalid(token, line, column):
            return "invalid \(token) at \(line):\(column)"
        case .nested(let errors):
            return errors.map(\.description).joined(separator: ";")
        }
    }
}
indirect enum Tree<Element> {
    case empty
    case leaf(Element)
    case node(left: Tree<Element>, right: Tree<Element>)
}
struct GenericBox<Element: Hashable>: Renderable, StorageProtocol where Element: CustomStringConvertible {
    typealias Body = [Element]
    @Clamped(0...100) var percentage: Int = 50
    private var items: [Element]
    let id: Identifier
    init(id: Identifier, items: [Element] = []) {
        self.id = id
        self.items = items
    }
    subscript(index: Int) -> Element {
        get { items[index] }
        set { items[index] = newValue }
    }
    var description: String { "\(id):\(items.count)" }
    func all() -> [Element] { items }
    func render() -> String {
        "box \(id)"
        "\(items)"
    }
    mutating func update(with body: [Element]) throws {
        guard !body.isEmpty else { throw LexerError.empty }
        defer { percentage = min(100, percentage + 1) }
        items = body
    }
    func map<T: Hashable>(_ transform: (Element) throws -> T) rethrows -> GenericBox<T> {
        GenericBox<T>(id: id, items: try items.map(transform))
    }
}
extension GenericBox: Sequence {
    func makeIterator() -> IndexingIterator<[Element]> {
        items.makeIterator()
    }
}
extension GenericBox where Element == String {
    var joined: String { items.joined(separator: ",") }
}
final class BaseClass {
    class var typeName: String { "BaseClass" }
    var name: String
    lazy var lazyValue: String = "lazy-\(name)"
    required init(name: String) {
        self.name = name
    }
    convenience init() {
        self.init(name: "default")
    }
    deinit {
        _ = name
    }
    func speak() -> String {
        "Base:\(name)"
    }
}
class DerivedClass: BaseClass, @unchecked Sendable {
    override class var typeName: String { "DerivedClass" }
    private var storage: [String: Any] = [:]
    override func speak() -> String {
        super.speak() + ":Derived"
    }
    subscript(dynamicMember member: String) -> Any? {
        get { storage[member] }
        set { storage[member] = newValue }
    }
}
@dynamicMemberLookup
struct DynamicLookup {
    var values: [String: Any]
    subscript(dynamicMember member: String) -> Any? {
        values[member]
    }
}
actor LexerActor {
    private var counter = 0
    nonisolated let name = "actor"
    func next() -> Int {
        counter += 1
        return counter
    }
    nonisolated func label() -> String {
        "LexerActor"
    }
}
@MainActor
final class MainActorBox {
    var value = 0
    func increment() {
        value += 1
    }
}
func normalFunction(_ a: Int, b: Int = 2, rest: Int...) -> Int {
    var sum = a + b
    for value in rest {
        sum += value
    }
    return sum
}
func throwingFunction(_ input: String?) throws -> String {
    guard let input, !input.isEmpty else {
        throw LexerError.empty
    }
    return input.uppercased()
}
func rethrowingFunction<T>(_ value: T, transform: (T) throws -> T) rethrows -> T {
    try transform(value)
}
func autoclosureFunction(_ predicate: @autoclosure () -> Bool) -> Bool {
    predicate()
}
func escapingFunction(_ block: @escaping @Sendable (Int) -> Void) {
    block(1)
}
func variadicGenericsLike<T, U>(_ pair: (T, U), values: T...) -> (T, U, [T]) {
    (pair.0, pair.1, values)
}
func asyncFunction() async throws -> String {
    try await Task.sleep(nanoseconds: 1)
    return "async"
}
func controlFlow(value: Int?) throws -> String {
    guard let value else { throw LexerError.empty }
    var result = ""
    outerLoop: for i in 0..<5 {
        innerLoop: for j in 0..<5 {
            if i == j { continue innerLoop }
            if i * j > value { break outerLoop }
            result += "\(i):\(j);"
        }
    }
    var counter = 0
    while counter < 3 {
        counter += 1
    }
    repeat {
        counter -= 1
    } while counter > 0
    switch value {
    case 0:
        return "zero"
    case 1, 2, 3:
        fallthrough
    case 4...10 where value.isMultiple(of: 2):
        return "small-even"
    case let x where x > 10:
        return "large \(x)"
    default:
        return result
    }
}
func patternMatching(_ any: Any) -> String {
    switch any {
    case is Int:
        return "int"
    case let string as String where string.hasPrefix("x"):
        return "x-string"
    case let (a, b) as (Int, Int):
        return "\(a),\(b)"
    case Optional<Any>.none:
        return "none"
    default:
        return "unknown"
    }
}
let closureOne: (Int) -> Int = { $0 + 1 }
let closureTwo = { (x: Int, y: Int) -> Int in
    let local = x * y
    return local + 1
}
let closureThree: () -> Void = { [decimalInteger] in
    _ = decimalInteger
}
let keyPath = \GenericBox<String>.id
let optionalChain = Optional(DerivedClass(name: "d"))?.speak()
let forced: String! = "implicitly unwrapped"
let forcedValue = forced!
let casted = dictionaryValue["alpha"] as? Int
let forcedCast = dictionaryValue["alpha"] as! Int
let metatypeValue: BaseClass.Type = DerivedClass.self
let anyType: Any.Type = Int.self
#if os(Linux)
let platform = "linux"
#elseif os(macOS)
let platform = "macos"
#else
let platform = "other"
#endif
if #available(macOS 13.0, iOS 16.0, *) {
    _ = "available"
} else {
    _ = "fallback"
}
let regexLiteral = #/^[A-Za-z_]\w*(?:\s*=\s*(?<value>.+))?$/#
let rawRegexLiteral = #/\/\*.*?\*\//#
let operatorSoup = (((1 + 2) * 3 - 4) / 5) % 6
let bitSoup = (0b1010 << 1) >> 1
let logicalSoup = true && false || !false
let nilCoalescing = nilValue ?? "fallback"
let ternaryValue = decimalInteger > 10 ? "large" : "small"
let namedTuple = (left: 1, middle: "two", right: true)
let (_, middle, _) = namedTuple
let `class` = "escaped keyword identifier"
let `switch` = "another escaped keyword"
// generated block 001: nested Swift syntax, patterns, closures, generics
struct Generated001<T: Hashable>: Sequence where T: CustomStringConvertible {
    var id: Int = 1
    var values: [T] = []
    var metadata: [String: Any] = [
        "name": "Generated001",
        "number": 1,
        "flags": [true, false, nil as Bool?],
    ]
    enum LocalEnum {
        case idle
        case running(Int)
        case failed(error: Error)
    }
    func makeIterator() -> IndexingIterator<[T]> {
        values.makeIterator()
    }
    mutating func append(_ value: T) {
        values.append(value)
    }
    func nestedMap<U: Hashable>(_ transform: (T) throws -> U) rethrows -> [U] {
        try values.map { value in
            try transform(value)
        }
    }
    func render(@TextBuilder _ content: () -> String) -> String {
        """
        Generated001:
        \(content())
        """
    }
    func control(_ input: Int?) throws -> String {
        guard let input else { throw LexerError.empty }
        var result = ""
        label_001: for outer in 0..<3 {
            for inner in 0..<3 {
                switch (outer, inner, input) {
                case (0, _, _):
                    result += "zero"
                case let (x, y, z) where (x + y + z + 1).isMultiple(of: 2):
                    result += "\(x):\(y):\(z)"
                default:
                    continue label_001
                }
            }
        }
        return result
    }
}
var generatedBox001 = Generated001<String>(values: ["a", "b", "c"])
generatedBox001.append("value-1")
let generatedResult001 = try? generatedBox001.control(1)
// generated block 002: nested Swift syntax, patterns, closures, generics
struct Generated002<T: Hashable>: Sequence where T: CustomStringConvertible {
    var id: Int = 2
    var values: [T] = []
    var metadata: [String: Any] = [
        "name": "Generated002",
        "number": 2,
        "flags": [true, false, nil as Bool?],
    ]
    enum LocalEnum {
        case idle
        case running(Int)
        case failed(error: Error)
    }
    func makeIterator() -> IndexingIterator<[T]> {
        values.makeIterator()
    }
    mutating func append(_ value: T) {
        values.append(value)
    }
    func nestedMap<U: Hashable>(_ transform: (T) throws -> U) rethrows -> [U] {
        try values.map { value in
            try transform(value)
        }
    }
    func render(@TextBuilder _ content: () -> String) -> String {
        """
        Generated002:
        \(content())
        """
    }
    func control(_ input: Int?) throws -> String {
        guard let input else { throw LexerError.empty }
        var result = ""
        label_002: for outer in 0..<3 {
            for inner in 0..<3 {
                switch (outer, inner, input) {
                case (0, _, _):
                    result += "zero"
                case let (x, y, z) where (x + y + z + 2).isMultiple(of: 2):
                    result += "\(x):\(y):\(z)"
                default:
                    continue label_002
                }
            }
        }
        return result
    }
}
var generatedBox002 = Generated002<String>(values: ["a", "b", "c"])
generatedBox002.append("value-2")
let generatedResult002 = try? generatedBox002.control(2)
// generated block 003: nested Swift syntax, patterns, closures, generics
struct Generated003<T: Hashable>: Sequence where T: CustomStringConvertible {
    var id: Int = 3
    var values: [T] = []
    var metadata: [String: Any] = [
        "name": "Generated003",
        "number": 3,
        "flags": [true, false, nil as Bool?],
    ]
    enum LocalEnum {
        case idle
        case running(Int)
        case failed(error: Error)
    }
    func makeIterator() -> IndexingIterator<[T]> {
        values.makeIterator()
    }
    mutating func append(_ value: T) {
        values.append(value)
    }
    func nestedMap<U: Hashable>(_ transform: (T) throws -> U) rethrows -> [U] {
        try values.map { value in
            try transform(value)
        }
    }
    func render(@TextBuilder _ content: () -> String) -> String {
        """
        Generated003:
        \(content())
        """
    }
    func control(_ input: Int?) throws -> String {
        guard let input else { throw LexerError.empty }
        var result = ""
        label_003: for outer in 0..<3 {
            for inner in 0..<3 {
                switch (outer, inner, input) {
                case (0, _, _):
                    result += "zero"
                case let (x, y, z) where (x + y + z + 3).isMultiple(of: 2):
                    result += "\(x):\(y):\(z)"
                default:
                    continue label_003
                }
            }
        }
        return result
    }
}
var generatedBox003 = Generated003<String>(values: ["a", "b", "c"])
generatedBox003.append("value-3")
let generatedResult003 = try? generatedBox003.control(3)
// generated block 004: nested Swift syntax, patterns, closures, generics
struct Generated004<T: Hashable>: Sequence where T: CustomStringConvertible {
    var id: Int = 4
    var values: [T] = []
    var metadata: [String: Any] = [
        "name": "Generated004",
        "number": 4,
        "flags": [true, false, nil as Bool?],
    ]
    enum LocalEnum {
        case idle
        case running(Int)
        case failed(error: Error)
    }
    func makeIterator() -> IndexingIterator<[T]> {
        values.makeIterator()
    }
    mutating func append(_ value: T) {
        values.append(value)
    }
    func nestedMap<U: Hashable>(_ transform: (T) throws -> U) rethrows -> [U] {
        try values.map { value in
            try transform(value)
        }
    }
    func render(@TextBuilder _ content: () -> String) -> String {
        """
        Generated004:
        \(content())
        """
    }
    func control(_ input: Int?) throws -> String {
        guard let input else { throw LexerError.empty }
        var result = ""
        label_004: for outer in 0..<3 {
            for inner in 0..<3 {
                switch (outer, inner, input) {
                case (0, _, _):
                    result += "zero"
                case let (x, y, z) where (x + y + z + 4).isMultiple(of: 2):
                    result += "\(x):\(y):\(z)"
                default:
                    continue label_004
                }
            }
        }
        return result
    }
}
var generatedBox004 = Generated004<String>(values: ["a", "b", "c"])
generatedBox004.append("value-4")
let generatedResult004 = try? generatedBox004.control(4)
// generated block 005: nested Swift syntax, patterns, closures, generics
struct Generated005<T: Hashable>: Sequence where T: CustomStringConvertible {
    var id: Int = 5
    var values: [T] = []
    var metadata: [String: Any] = [
        "name": "Generated005",
        "number": 5,
        "flags": [true, false, nil as Bool?],
    ]
    enum LocalEnum {
        case idle
        case running(Int)
        case failed(error: Error)
    }
    func makeIterator() -> IndexingIterator<[T]> {
        values.makeIterator()
    }
    mutating func append(_ value: T) {
        values.append(value)
    }
    func nestedMap<U: Hashable>(_ transform: (T) throws -> U) rethrows -> [U] {
        try values.map { value in
            try transform(value)
        }
    }
    func render(@TextBuilder _ content: () -> String) -> String {
        """
        Generated005:
        \(content())
        """
    }
    func control(_ input: Int?) throws -> String {
        guard let input else { throw LexerError.empty }
        var result = ""
        label_005: for outer in 0..<3 {
            for inner in 0..<3 {
                switch (outer, inner, input) {
                case (0, _, _):
                    result += "zero"
                case let (x, y, z) where (x + y + z + 5).isMultiple(of: 2):
                    result += "\(x):\(y):\(z)"
                default:
                    continue label_005
                }
            }
        }
        return result
    }
}
var generatedBox005 = Generated005<String>(values: ["a", "b", "c"])
generatedBox005.append("value-5")
let generatedResult005 = try? generatedBox005.control(5)
// generated block 006: nested Swift syntax, patterns, closures, generics
struct Generated006<T: Hashable>: Sequence where T: CustomStringConvertible {
    var id: Int = 6
    var values: [T] = []
    var metadata: [String: Any] = [
        "name": "Generated006",
        "number": 6,
        "flags": [true, false, nil as Bool?],
    ]
    enum LocalEnum {
        case idle
        case running(Int)
        case failed(error: Error)
    }
    func makeIterator() -> IndexingIterator<[T]> {
        values.makeIterator()
    }
    mutating func append(_ value: T) {
        values.append(value)
    }
    func nestedMap<U: Hashable>(_ transform: (T) throws -> U) rethrows -> [U] {
        try values.map { value in
            try transform(value)
        }
    }
    func render(@TextBuilder _ content: () -> String) -> String {
        """
        Generated006:
        \(content())
        """
    }
    func control(_ input: Int?) throws -> String {
        guard let input else { throw LexerError.empty }
        var result = ""
        label_006: for outer in 0..<3 {
            for inner in 0..<3 {
                switch (outer, inner, input) {
                case (0, _, _):
                    result += "zero"
                case let (x, y, z) where (x + y + z + 6).isMultiple(of: 2):
                    result += "\(x):\(y):\(z)"
                default:
                    continue label_006
                }
            }
        }
        return result
    }
}
var generatedBox006 = Generated006<String>(values: ["a", "b", "c"])
generatedBox006.append("value-6")
let generatedResult006 = try? generatedBox006.control(6)
// generated block 007: nested Swift syntax, patterns, closures, generics
struct Generated007<T: Hashable>: Sequence where T: CustomStringConvertible {
    var id: Int = 7
    var values: [T] = []
    var metadata: [String: Any] = [
        "name": "Generated007",
        "number": 7,
        "flags": [true, false, nil as Bool?],
    ]
    enum LocalEnum {
        case idle
        case running(Int)
        case failed(error: Error)
    }
    func makeIterator() -> IndexingIterator<[T]> {
        values.makeIterator()
    }
    mutating func append(_ value: T) {
        values.append(value)
    }
    func nestedMap<U: Hashable>(_ transform: (T) throws -> U) rethrows -> [U] {
        try values.map { value in
            try transform(value)
        }
    }
    func render(@TextBuilder _ content: () -> String) -> String {
        """
        Generated007:
        \(content())
        """
    }
    func control(_ input: Int?) throws -> String {
        guard let input else { throw LexerError.empty }
        var result = ""
        label_007: for outer in 0..<3 {
            for inner in 0..<3 {
                switch (outer, inner, input) {
                case (0, _, _):
                    result += "zero"
                case let (x, y, z) where (x + y + z + 7).isMultiple(of: 2):
                    result += "\(x):\(y):\(z)"
                default:
                    continue label_007
                }
            }
        }
        return result
    }
}
var generatedBox007 = Generated007<String>(values: ["a", "b", "c"])
generatedBox007.append("value-7")
let generatedResult007 = try? generatedBox007.control(7)
// generated block 008: nested Swift syntax, patterns, closures, generics
struct Generated008<T: Hashable>: Sequence where T: CustomStringConvertible {
    var id: Int = 8
    var values: [T] = []
    var metadata: [String: Any] = [
        "name": "Generated008",
        "number": 8,
        "flags": [true, false, nil as Bool?],
    ]
    enum LocalEnum {
        case idle
        case running(Int)
        case failed(error: Error)
    }
    func makeIterator() -> IndexingIterator<[T]> {
        values.makeIterator()
    }
    mutating func append(_ value: T) {
        values.append(value)
    }
    func nestedMap<U: Hashable>(_ transform: (T) throws -> U) rethrows -> [U] {
        try values.map { value in
            try transform(value)
        }
    }
    func render(@TextBuilder _ content: () -> String) -> String {
        """
        Generated008:
        \(content())
        """
    }
    func control(_ input: Int?) throws -> String {
        guard let input else { throw LexerError.empty }
        var result = ""
        label_008: for outer in 0..<3 {
            for inner in 0..<3 {
                switch (outer, inner, input) {
                case (0, _, _):
                    result += "zero"
                case let (x, y, z) where (x + y + z + 8).isMultiple(of: 2):
                    result += "\(x):\(y):\(z)"
                default:
                    continue label_008
                }
            }
        }
        return result
    }
}
var generatedBox008 = Generated008<String>(values: ["a", "b", "c"])
generatedBox008.append("value-8")
let generatedResult008 = try? generatedBox008.control(8)
// generated block 009: nested Swift syntax, patterns, closures, generics
struct Generated009<T: Hashable>: Sequence where T: CustomStringConvertible {
    var id: Int = 9
    var values: [T] = []
    var metadata: [String: Any] = [
        "name": "Generated009",
        "number": 9,
        "flags": [true, false, nil as Bool?],
    ]
    enum LocalEnum {
        case idle
        case running(Int)
        case failed(error: Error)
    }
    func makeIterator() -> IndexingIterator<[T]> {
        values.makeIterator()
    }
    mutating func append(_ value: T) {
        values.append(value)
    }
    func nestedMap<U: Hashable>(_ transform: (T) throws -> U) rethrows -> [U] {
        try values.map { value in
            try transform(value)
        }
    }
    func render(@TextBuilder _ content: () -> String) -> String {
        """
        Generated009:
        \(content())
        """
    }
    func control(_ input: Int?) throws -> String {
        guard let input else { throw LexerError.empty }
        var result = ""
        label_009: for outer in 0..<3 {
            for inner in 0..<3 {
                switch (outer, inner, input) {
                case (0, _, _):
                    result += "zero"
                case let (x, y, z) where (x + y + z + 9).isMultiple(of: 2):
                    result += "\(x):\(y):\(z)"
                default:
                    continue label_009
                }
            }
        }
        return result
    }
}
var generatedBox009 = Generated009<String>(values: ["a", "b", "c"])
generatedBox009.append("value-9")
let generatedResult009 = try? generatedBox009.control(9)
// generated block 010: nested Swift syntax, patterns, closures, generics
struct Generated010<T: Hashable>: Sequence where T: CustomStringConvertible {
    var id: Int = 10
    var values: [T] = []
    var metadata: [String: Any] = [
        "name": "Generated010",
        "number": 10,
        "flags": [true, false, nil as Bool?],
    ]
    enum LocalEnum {
        case idle
        case running(Int)
        case failed(error: Error)
    }
    func makeIterator() -> IndexingIterator<[T]> {
        values.makeIterator()
    }
    mutating func append(_ value: T) {
        values.append(value)
    }
    func nestedMap<U: Hashable>(_ transform: (T) throws -> U) rethrows -> [U] {
        try values.map { value in
            try transform(value)
        }
    }
    func render(@TextBuilder _ content: () -> String) -> String {
        """
        Generated010:
        \(content())
        """
    }
    func control(_ input: Int?) throws -> String {
        guard let input else { throw LexerError.empty }
        var result = ""
        label_010: for outer in 0..<3 {
            for inner in 0..<3 {
                switch (outer, inner, input) {
                case (0, _, _):
                    result += "zero"
                case let (x, y, z) where (x + y + z + 10).isMultiple(of: 2):
                    result += "\(x):\(y):\(z)"
                default:
                    continue label_010
                }
            }
        }
        return result
    }
}
var generatedBox010 = Generated010<String>(values: ["a", "b", "c"])
generatedBox010.append("value-10")
let generatedResult010 = try? generatedBox010.control(10)
// generated block 011: nested Swift syntax, patterns, closures, generics
struct Generated011<T: Hashable>: Sequence where T: CustomStringConvertible {
    var id: Int = 11
    var values: [T] = []
    var metadata: [String: Any] = [
        "name": "Generated011",
        "number": 11,
        "flags": [true, false, nil as Bool?],
    ]
    enum LocalEnum {
        case idle
        case running(Int)
        case failed(error: Error)
    }
    func makeIterator() -> IndexingIterator<[T]> {
        values.makeIterator()
    }
    mutating func append(_ value: T) {
        values.append(value)
    }
    func nestedMap<U: Hashable>(_ transform: (T) throws -> U) rethrows -> [U] {
        try values.map { value in
            try transform(value)
        }
    }
    func render(@TextBuilder _ content: () -> String) -> String {
        """
        Generated011:
        \(content())
        """
    }
    func control(_ input: Int?) throws -> String {
        guard let input else { throw LexerError.empty }
        var result = ""
        label_011: for outer in 0..<3 {
            for inner in 0..<3 {
                switch (outer, inner, input) {
                case (0, _, _):
                    result += "zero"
                case let (x, y, z) where (x + y + z + 11).isMultiple(of: 2):
                    result += "\(x):\(y):\(z)"
                default:
                    continue label_011
                }
            }
        }
        return result
    }
}
var generatedBox011 = Generated011<String>(values: ["a", "b", "c"])
generatedBox011.append("value-11")
let generatedResult011 = try? generatedBox011.control(11)
// generated block 012: nested Swift syntax, patterns, closures, generics
struct Generated012<T: Hashable>: Sequence where T: CustomStringConvertible {
    var id: Int = 12
    var values: [T] = []
    var metadata: [String: Any] = [
        "name": "Generated012",
        "number": 12,
        "flags": [true, false, nil as Bool?],
    ]
    enum LocalEnum {
        case idle
        case running(Int)
        case failed(error: Error)
    }
    func makeIterator() -> IndexingIterator<[T]> {
        values.makeIterator()
    }
    mutating func append(_ value: T) {
        values.append(value)
    }
    func nestedMap<U: Hashable>(_ transform: (T) throws -> U) rethrows -> [U] {
        try values.map { value in
            try transform(value)
        }
    }
    func render(@TextBuilder _ content: () -> String) -> String {
        """
        Generated012:
        \(content())
        """
    }
    func control(_ input: Int?) throws -> String {
        guard let input else { throw LexerError.empty }
        var result = ""
        label_012: for outer in 0..<3 {
            for inner in 0..<3 {
                switch (outer, inner, input) {
                case (0, _, _):
                    result += "zero"
                case let (x, y, z) where (x + y + z + 12).isMultiple(of: 2):
                    result += "\(x):\(y):\(z)"
                default:
                    continue label_012
                }
            }
        }
        return result
    }
}
var generatedBox012 = Generated012<String>(values: ["a", "b", "c"])
generatedBox012.append("value-12")
let generatedResult012 = try? generatedBox012.control(12)
// generated block 013: nested Swift syntax, patterns, closures, generics
struct Generated013<T: Hashable>: Sequence where T: CustomStringConvertible {
    var id: Int = 13
    var values: [T] = []
    var metadata: [String: Any] = [
        "name": "Generated013",
        "number": 13,
        "flags": [true, false, nil as Bool?],
    ]
    enum LocalEnum {
        case idle
        case running(Int)
        case failed(error: Error)
    }
    func makeIterator() -> IndexingIterator<[T]> {
        values.makeIterator()
    }
    mutating func append(_ value: T) {
        values.append(value)
    }
    func nestedMap<U: Hashable>(_ transform: (T) throws -> U) rethrows -> [U] {
        try values.map { value in
            try transform(value)
        }
    }
    func render(@TextBuilder _ content: () -> String) -> String {
        """
        Generated013:
        \(content())
        """
    }
    func control(_ input: Int?) throws -> String {
        guard let input else { throw LexerError.empty }
        var result = ""
        label_013: for outer in 0..<3 {
            for inner in 0..<3 {
                switch (outer, inner, input) {
                case (0, _, _):
                    result += "zero"
                case let (x, y, z) where (x + y + z + 13).isMultiple(of: 2):
                    result += "\(x):\(y):\(z)"
                default:
                    continue label_013
                }
            }
        }
        return result
    }
}
var generatedBox013 = Generated013<String>(values: ["a", "b", "c"])
generatedBox013.append("value-13")
let generatedResult013 = try? generatedBox013.control(13)
// generated block 014: nested Swift syntax, patterns, closures, generics
struct Generated014<T: Hashable>: Sequence where T: CustomStringConvertible {
    var id: Int = 14
}
