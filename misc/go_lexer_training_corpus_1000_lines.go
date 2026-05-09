// go_lexer_training_corpus_1000_lines.go
// Synthetic Go lexer-training corpus; not intended to compile or run.
/* block comment: if x := 1; x < 2 { fmt.Println("not code") } */
package main
import (
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"math"
	"math/cmplx"
	"os"
	"reflect"
	"regexp"
	"sort"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"time"
	"unicode/utf8"
)
const (
	DecimalInteger = 123_456
	BinaryInteger  = 0b1010_0101
	OctalInteger   = 0o755
	LegacyOctal    = 0755
	HexInteger     = 0xDEAD_BEEF
	FloatValue     = 123.456e-7
	HexFloatValue  = 0x1.8p+2
	ImaginaryValue = 1.5i
	StringValue    = "normal string\nwith\t\"quotes\" and \\ slash"
	RawStringValue = `raw string \n not newline with /* not comment */`
	RuneValue      = 'ß'
	EscapedRune    = '\U0001F600'
	ByteValue      = byte('a')
)
var (
	globalCounter int64
	globalMap     = map[string]any{"alpha": 1, "beta": []int{1, 2, 3}}
	globalSlice   = []string{"one", "two", "three"}
	globalArray   = [...]int{0: 1, 2: 3, 4: 5}
	globalChan    = make(chan string, 4)
	globalMutex   sync.Mutex
)
type TokenKind int
const (
	TokenNone TokenKind = iota
	TokenIdentifier
	TokenNumber
	TokenString
	TokenRune
	TokenOperator
	TokenComment
	TokenEOF
)
type Position struct {
	Line   int `json:"line" db:"line_column"`
	Column int `json:"column" db:"column_column"`
}
type Token struct {
	Kind     TokenKind       `json:"kind"`
	Position Position        `json:"position"`
	Text     string          `json:"text"`
	Value    any             `json:"value,omitempty"`
	Flags    map[string]bool `json:"flags,omitempty"`
}
type Number interface {
	~int | ~int64 | ~float64 | ~complex128
}
type Renderer interface {
	Render() string
	fmt.Stringer
}
type Storage[T any] interface {
	Push(T)
	Get(int) (T, bool)
	All() []T
}
type GenericBox[T comparable] struct {
	ID       string
	Values   []T
	Index    map[T]int
	Metadata map[string]any
}
func NewGenericBox[T comparable](id string, values ...T) *GenericBox[T] {
	box := &GenericBox[T]{ID: id, Values: make([]T, 0, len(values)), Index: map[T]int{}, Metadata: map[string]any{"created": time.Now()}}
	for _, value := range values {
		box.Push(value)
	}
	return box
}
func (b *GenericBox[T]) Push(value T) {
	b.Index[value] = len(b.Values)
	b.Values = append(b.Values, value)
}
func (b *GenericBox[T]) Get(index int) (T, bool) {
	var zero T
	if index < 0 || index >= len(b.Values) {
		return zero, false
	}
	return b.Values[index], true
}
func (b *GenericBox[T]) All() []T {
	return append([]T(nil), b.Values...)
}
func (b *GenericBox[T]) String() string {
	return fmt.Sprintf("%s:%d", b.ID, len(b.Values))
}
func (b *GenericBox[T]) Render() string {
	builder := strings.Builder{}
	builder.WriteString(b.ID)
	for _, value := range b.Values {
		builder.WriteString(fmt.Sprintf(":%v", value))
	}
	return builder.String()
}
type Tree[T any] interface { isTree() }
type Empty[T any] struct{}
func (Empty[T]) isTree() {}
type Leaf[T any] struct { Value T }
func (Leaf[T]) isTree() {}
type Node[T any] struct { Left, Right Tree[T] }
func (Node[T]) isTree() {}
type LexerError struct {
	Token  string
	Line   int
	Column int
	Cause  error
}
func (e LexerError) Error() string {
	if e.Cause != nil {
		return fmt.Sprintf("invalid %q at %d:%d: %v", e.Token, e.Line, e.Column, e.Cause)
	}
	return fmt.Sprintf("invalid %q at %d:%d", e.Token, e.Line, e.Column)
}
func Add[T Number](left, right T) T { return left + right }
func MapSlice[T any, U any](input []T, fn func(T) U) []U {
	output := make([]U, 0, len(input))
	for _, item := range input {
		output = append(output, fn(item))
	}
	return output
}
func normalFunction(a int, b ...int) (int, error) {
	total := a
	for _, value := range b {
		total += value
	}
	if total < 0 {
		return 0, errors.New("negative total")
	}
	return total, nil
}
func namedReturns(input string) (trimmed string, length int, err error) {
	defer func() {
		if recovered := recover(); recovered != nil {
			err = fmt.Errorf("panic: %v", recovered)
		}
	}()
	trimmed = strings.TrimSpace(input)
	length = len(trimmed)
	if length == 0 {
		err = LexerError{Token: input, Line: 1, Column: 1}
	}
	return
}
func controlFlow(input int) string {
	result := strings.Builder{}
Outer:
	for i := 0; i < 5; i++ {
		for j := 4; j >= 0; j-- {
			if i == j {
				continue
			} else if i*j > input {
				break Outer
			} else {
				result.WriteString(fmt.Sprintf("%d:%d;", i, j))
			}
		}
	}
	counter := 0
	for counter < 3 {
		counter++
	}
	for {
		counter--
		if counter <= 0 {
			break
		}
	}
	switch {
	case input == 0:
		return "zero"
	case input >= 1 && input <= 3:
		fallthrough
	case input%2 == 0:
		return "small-even"
	default:
		return result.String()
	}
}
func typeSwitch(value any) string {
	switch v := value.(type) {
	case nil:
		return "nil"
	case int:
		return fmt.Sprintf("int:%d", v)
	case string:
		return "string:" + v
	case []int:
		return fmt.Sprintf("slice:%d", len(v))
	case interface{ Render() string }:
		return v.Render()
	default:
		return fmt.Sprintf("unknown:%T", v)
	}
}
func selectExample(ctx context.Context, input <-chan int, output chan<- string) {
	ticker := time.NewTicker(time.Millisecond)
	defer ticker.Stop()
	for {
		select {
		case <-ctx.Done():
			return
		case value, ok := <-input:
			if !ok {
				close(output)
				return
			}
			output <- fmt.Sprintf("value=%d", value)
		case <-ticker.C:
			output <- "tick"
		default:
			time.Sleep(time.Nanosecond)
		}
	}
}
func goroutineExample() {
	var wg sync.WaitGroup
	input := make(chan int, 3)
	output := make(chan string, 3)
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	wg.Add(1)
	go func() {
		defer wg.Done()
		selectExample(ctx, input, output)
	}()
	for i := 0; i < 3; i++ {
		input <- i
	}
	close(input)
	cancel()
	wg.Wait()
	_ = output
}
func pointerAndSliceExamples() {
	value := 42
	pointer := &value
	*pointer++
	slice := []int{1, 2, 3, 4, 5}
	sub := slice[1:4:5]
	array := [3]int{1, 2, 3}
	arrayPointer := &array
	_ = (*arrayPointer)[0]
	_ = sub
}
func literalExamples() {
	raw := `raw literal with multiple lines marker and ${notInterpolation}`
	interpreted := "line\nwith\tescapes\x41\u00E4\U0001F600"
	runes := []rune{'a', 'ä', '\n', '\x41', '\u00E4', '\U0001F600'}
	bytesValue := []byte{0x00, 0xff, 'A', '\n'}
	commentLike := "/* not comment */ // not comment"
	regexLike := `/not/a/go/regex/`
	_ = []any{raw, interpreted, runes, bytesValue, commentLike, regexLike}
}
func mapAndStructExamples() {
	token := Token{Kind: TokenIdentifier, Position: Position{Line: 10, Column: 20}, Text: "identifier", Value: 123, Flags: map[string]bool{"escaped": false, "valid": true}}
	encoded, _ := json.MarshalIndent(token, "", "  ")
	decoded := map[string]any{}
	_ = json.Unmarshal(encoded, &decoded)
}
func reflectionExamples(value any) []string {
	rv := reflect.ValueOf(value)
	rt := reflect.TypeOf(value)
	return []string{rv.Kind().String(), rt.String()}
}
func deferPanicRecoverExample() (result string) {
	defer func() {
		if recovered := recover(); recovered != nil {
			result = fmt.Sprintf("recovered:%v", recovered)
		}
	}()
	panic("synthetic panic")
}
func regexpExamples(input string) []string {
	re := regexp.MustCompile(`(?P<word>[A-Za-z_]\w*)\s*=\s*(?P<value>.+)`)
	return re.FindStringSubmatch(input)
}
func numericOperatorExamples() {
	x := 0b1010_0101
	x <<= 1; x >>= 1; x &= 0xff; x |= 0x10; x ^= 0x01; x &^= 0x02
	y := +x - 2*3/4%5
	z := complex(1.0, 2.0) + complex(3.0, 4.0)
	_ = []any{x, y, z, cmplx.Abs(z), math.Sqrt(float64(y))}
}
func sortingExamples() {
	values := []int{3, 1, 4, 1, 5, 9}
	sort.Slice(values, func(i, j int) bool { return values[i] < values[j] })
	sort.SliceStable(values, func(i, j int) bool { return values[i]%2 < values[j]%2 })
}
func ioExamples(reader io.Reader, writer io.Writer) error {
	buffer := bytes.Buffer{}
	if _, err := io.Copy(&buffer, reader); err != nil {
		return err
	}
	_, err := writer.Write(buffer.Bytes())
	return err
}
func atomicExamples() {
	atomic.AddInt64(&globalCounter, 1)
	atomic.StoreInt64(&globalCounter, atomic.LoadInt64(&globalCounter)+1)
}
func init() { globalChan <- "init" }
type GeneratedStruct001[T comparable] struct {
	ID       int
	Name     string
	Values   []T
	Index    map[T]int
	Metadata map[string]any
}
func NewGeneratedStruct001[T comparable](name string, values ...T) *GeneratedStruct001[T] {
	instance := &GeneratedStruct001[T]{ID: 1, Name: name, Values: make([]T, 0, len(values)), Index: map[T]int{}, Metadata: map[string]any{"block": 1, "name": name}}
	for _, value := range values { instance.Push(value) }
	return instance
}
func (g *GeneratedStruct001[T]) Push(value T) {
	g.Index[value] = len(g.Values)
	g.Values = append(g.Values, value)
}
func (g *GeneratedStruct001[T]) Nested(input int) (int, error) {
	total := 0
GeneratedLoop001:
	for outer := 0; outer < 3; outer++ {
		for inner := 0; inner < 3; inner++ {
			switch {
			case (outer+inner+input+1)%2 == 0:
				total += outer*inner + 1
			case inner == 1:
				total += input
			default:
				continue GeneratedLoop001
			}
		}
	}
	if total < 0 { return 0, LexerError{Token: g.Name, Line: 1, Column: total} }
	return total, nil
}
func GeneratedFunction001() int {
	generated := NewGeneratedStruct001[int]("GeneratedStruct001", 1, 2, 3)
	generated.Push(1)
	value, err := generated.Nested(1)
	if err != nil { return -1 }
	return value + len(generated.Values)
}
type GeneratedStruct002[T comparable] struct {
	ID       int
	Name     string
	Values   []T
	Index    map[T]int
	Metadata map[string]any
}
func NewGeneratedStruct002[T comparable](name string, values ...T) *GeneratedStruct002[T] {
	instance := &GeneratedStruct002[T]{ID: 2, Name: name, Values: make([]T, 0, len(values)), Index: map[T]int{}, Metadata: map[string]any{"block": 2, "name": name}}
	for _, value := range values { instance.Push(value) }
	return instance
}
func (g *GeneratedStruct002[T]) Push(value T) {
	g.Index[value] = len(g.Values)
	g.Values = append(g.Values, value)
}
func (g *GeneratedStruct002[T]) Nested(input int) (int, error) {
	total := 0
GeneratedLoop002:
	for outer := 0; outer < 3; outer++ {
		for inner := 0; inner < 3; inner++ {
			switch {
			case (outer+inner+input+2)%2 == 0:
				total += outer*inner + 2
			case inner == 1:
				total += input
			default:
				continue GeneratedLoop002
			}
		}
	}
	if total < 0 { return 0, LexerError{Token: g.Name, Line: 2, Column: total} }
	return total, nil
}
func GeneratedFunction002() int {
	generated := NewGeneratedStruct002[int]("GeneratedStruct002", 1, 2, 3)
	generated.Push(2)
	value, err := generated.Nested(2)
	if err != nil { return -2 }
	return value + len(generated.Values)
}
type GeneratedStruct003[T comparable] struct {
	ID       int
	Name     string
	Values   []T
	Index    map[T]int
	Metadata map[string]any
}
func NewGeneratedStruct003[T comparable](name string, values ...T) *GeneratedStruct003[T] {
	instance := &GeneratedStruct003[T]{ID: 3, Name: name, Values: make([]T, 0, len(values)), Index: map[T]int{}, Metadata: map[string]any{"block": 3, "name": name}}
	for _, value := range values { instance.Push(value) }
	return instance
}
func (g *GeneratedStruct003[T]) Push(value T) {
	g.Index[value] = len(g.Values)
	g.Values = append(g.Values, value)
}
func (g *GeneratedStruct003[T]) Nested(input int) (int, error) {
	total := 0
GeneratedLoop003:
	for outer := 0; outer < 3; outer++ {
		for inner := 0; inner < 3; inner++ {
			switch {
			case (outer+inner+input+3)%2 == 0:
				total += outer*inner + 3
			case inner == 1:
				total += input
			default:
				continue GeneratedLoop003
			}
		}
	}
	if total < 0 { return 0, LexerError{Token: g.Name, Line: 3, Column: total} }
	return total, nil
}
func GeneratedFunction003() int {
	generated := NewGeneratedStruct003[int]("GeneratedStruct003", 1, 2, 3)
	generated.Push(3)
	value, err := generated.Nested(3)
	if err != nil { return -3 }
	return value + len(generated.Values)
}
type GeneratedStruct004[T comparable] struct {
	ID       int
	Name     string
	Values   []T
	Index    map[T]int
	Metadata map[string]any
}
func NewGeneratedStruct004[T comparable](name string, values ...T) *GeneratedStruct004[T] {
	instance := &GeneratedStruct004[T]{ID: 4, Name: name, Values: make([]T, 0, len(values)), Index: map[T]int{}, Metadata: map[string]any{"block": 4, "name": name}}
	for _, value := range values { instance.Push(value) }
	return instance
}
func (g *GeneratedStruct004[T]) Push(value T) {
	g.Index[value] = len(g.Values)
	g.Values = append(g.Values, value)
}
func (g *GeneratedStruct004[T]) Nested(input int) (int, error) {
	total := 0
GeneratedLoop004:
	for outer := 0; outer < 3; outer++ {
		for inner := 0; inner < 3; inner++ {
			switch {
			case (outer+inner+input+4)%2 == 0:
				total += outer*inner + 4
			case inner == 1:
				total += input
			default:
				continue GeneratedLoop004
			}
		}
	}
	if total < 0 { return 0, LexerError{Token: g.Name, Line: 4, Column: total} }
	return total, nil
}
func GeneratedFunction004() int {
	generated := NewGeneratedStruct004[int]("GeneratedStruct004", 1, 2, 3)
	generated.Push(4)
	value, err := generated.Nested(4)
	if err != nil { return -4 }
	return value + len(generated.Values)
}
type GeneratedStruct005[T comparable] struct {
	ID       int
	Name     string
	Values   []T
	Index    map[T]int
	Metadata map[string]any
}
func NewGeneratedStruct005[T comparable](name string, values ...T) *GeneratedStruct005[T] {
	instance := &GeneratedStruct005[T]{ID: 5, Name: name, Values: make([]T, 0, len(values)), Index: map[T]int{}, Metadata: map[string]any{"block": 5, "name": name}}
	for _, value := range values { instance.Push(value) }
	return instance
}
func (g *GeneratedStruct005[T]) Push(value T) {
	g.Index[value] = len(g.Values)
	g.Values = append(g.Values, value)
}
func (g *GeneratedStruct005[T]) Nested(input int) (int, error) {
	total := 0
GeneratedLoop005:
	for outer := 0; outer < 3; outer++ {
		for inner := 0; inner < 3; inner++ {
			switch {
			case (outer+inner+input+5)%2 == 0:
				total += outer*inner + 5
			case inner == 1:
				total += input
			default:
				continue GeneratedLoop005
			}
		}
	}
	if total < 0 { return 0, LexerError{Token: g.Name, Line: 5, Column: total} }
	return total, nil
}
func GeneratedFunction005() int {
	generated := NewGeneratedStruct005[int]("GeneratedStruct005", 1, 2, 3)
	generated.Push(5)
	value, err := generated.Nested(5)
	if err != nil { return -5 }
	return value + len(generated.Values)
}
type GeneratedStruct006[T comparable] struct {
	ID       int
	Name     string
	Values   []T
	Index    map[T]int
	Metadata map[string]any
}
func NewGeneratedStruct006[T comparable](name string, values ...T) *GeneratedStruct006[T] {
	instance := &GeneratedStruct006[T]{ID: 6, Name: name, Values: make([]T, 0, len(values)), Index: map[T]int{}, Metadata: map[string]any{"block": 6, "name": name}}
	for _, value := range values { instance.Push(value) }
	return instance
}
func (g *GeneratedStruct006[T]) Push(value T) {
	g.Index[value] = len(g.Values)
	g.Values = append(g.Values, value)
}
func (g *GeneratedStruct006[T]) Nested(input int) (int, error) {
	total := 0
GeneratedLoop006:
	for outer := 0; outer < 3; outer++ {
		for inner := 0; inner < 3; inner++ {
			switch {
			case (outer+inner+input+6)%2 == 0:
				total += outer*inner + 6
			case inner == 1:
				total += input
			default:
				continue GeneratedLoop006
			}
		}
	}
	if total < 0 { return 0, LexerError{Token: g.Name, Line: 6, Column: total} }
	return total, nil
}
func GeneratedFunction006() int {
	generated := NewGeneratedStruct006[int]("GeneratedStruct006", 1, 2, 3)
	generated.Push(6)
	value, err := generated.Nested(6)
	if err != nil { return -6 }
	return value + len(generated.Values)
}
type GeneratedStruct007[T comparable] struct {
	ID       int
	Name     string
	Values   []T
	Index    map[T]int
	Metadata map[string]any
}
func NewGeneratedStruct007[T comparable](name string, values ...T) *GeneratedStruct007[T] {
	instance := &GeneratedStruct007[T]{ID: 7, Name: name, Values: make([]T, 0, len(values)), Index: map[T]int{}, Metadata: map[string]any{"block": 7, "name": name}}
	for _, value := range values { instance.Push(value) }
	return instance
}
func (g *GeneratedStruct007[T]) Push(value T) {
	g.Index[value] = len(g.Values)
	g.Values = append(g.Values, value)
}
func (g *GeneratedStruct007[T]) Nested(input int) (int, error) {
	total := 0
GeneratedLoop007:
	for outer := 0; outer < 3; outer++ {
		for inner := 0; inner < 3; inner++ {
			switch {
			case (outer+inner+input+7)%2 == 0:
				total += outer*inner + 7
			case inner == 1:
				total += input
			default:
				continue GeneratedLoop007
			}
		}
	}
	if total < 0 { return 0, LexerError{Token: g.Name, Line: 7, Column: total} }
	return total, nil
}
func GeneratedFunction007() int {
	generated := NewGeneratedStruct007[int]("GeneratedStruct007", 1, 2, 3)
	generated.Push(7)
	value, err := generated.Nested(7)
	if err != nil { return -7 }
	return value + len(generated.Values)
}
type GeneratedStruct008[T comparable] struct {
	ID       int
	Name     string
	Values   []T
	Index    map[T]int
	Metadata map[string]any
}
func NewGeneratedStruct008[T comparable](name string, values ...T) *GeneratedStruct008[T] {
	instance := &GeneratedStruct008[T]{ID: 8, Name: name, Values: make([]T, 0, len(values)), Index: map[T]int{}, Metadata: map[string]any{"block": 8, "name": name}}
	for _, value := range values { instance.Push(value) }
	return instance
}
func (g *GeneratedStruct008[T]) Push(value T) {
	g.Index[value] = len(g.Values)
	g.Values = append(g.Values, value)
}
func (g *GeneratedStruct008[T]) Nested(input int) (int, error) {
	total := 0
GeneratedLoop008:
	for outer := 0; outer < 3; outer++ {
		for inner := 0; inner < 3; inner++ {
			switch {
			case (outer+inner+input+8)%2 == 0:
				total += outer*inner + 8
			case inner == 1:
				total += input
			default:
				continue GeneratedLoop008
			}
		}
	}
	if total < 0 { return 0, LexerError{Token: g.Name, Line: 8, Column: total} }
	return total, nil
}
func GeneratedFunction008() int {
	generated := NewGeneratedStruct008[int]("GeneratedStruct008", 1, 2, 3)
	generated.Push(8)
	value, err := generated.Nested(8)
	if err != nil { return -8 }
	return value + len(generated.Values)
}
type GeneratedStruct009[T comparable] struct {
	ID       int
	Name     string
	Values   []T
	Index    map[T]int
	Metadata map[string]any
}
func NewGeneratedStruct009[T comparable](name string, values ...T) *GeneratedStruct009[T] {
	instance := &GeneratedStruct009[T]{ID: 9, Name: name, Values: make([]T, 0, len(values)), Index: map[T]int{}, Metadata: map[string]any{"block": 9, "name": name}}
	for _, value := range values { instance.Push(value) }
	return instance
}
func (g *GeneratedStruct009[T]) Push(value T) {
	g.Index[value] = len(g.Values)
	g.Values = append(g.Values, value)
}
func (g *GeneratedStruct009[T]) Nested(input int) (int, error) {
	total := 0
GeneratedLoop009:
	for outer := 0; outer < 3; outer++ {
		for inner := 0; inner < 3; inner++ {
			switch {
			case (outer+inner+input+9)%2 == 0:
				total += outer*inner + 9
			case inner == 1:
				total += input
			default:
				continue GeneratedLoop009
			}
		}
	}
	if total < 0 { return 0, LexerError{Token: g.Name, Line: 9, Column: total} }
	return total, nil
}
func GeneratedFunction009() int {
	generated := NewGeneratedStruct009[int]("GeneratedStruct009", 1, 2, 3)
	generated.Push(9)
	value, err := generated.Nested(9)
	if err != nil { return -9 }
	return value + len(generated.Values)
}
type GeneratedStruct010[T comparable] struct {
	ID       int
	Name     string
	Values   []T
	Index    map[T]int
	Metadata map[string]any
}
func NewGeneratedStruct010[T comparable](name string, values ...T) *GeneratedStruct010[T] {
	instance := &GeneratedStruct010[T]{ID: 10, Name: name, Values: make([]T, 0, len(values)), Index: map[T]int{}, Metadata: map[string]any{"block": 10, "name": name}}
	for _, value := range values { instance.Push(value) }
	return instance
}
func (g *GeneratedStruct010[T]) Push(value T) {
	g.Index[value] = len(g.Values)
	g.Values = append(g.Values, value)
}
func (g *GeneratedStruct010[T]) Nested(input int) (int, error) {
	total := 0
GeneratedLoop010:
	for outer := 0; outer < 3; outer++ {
		for inner := 0; inner < 3; inner++ {
			switch {
			case (outer+inner+input+10)%2 == 0:
				total += outer*inner + 10
			case inner == 1:
				total += input
			default:
				continue GeneratedLoop010
			}
		}
	}
	if total < 0 { return 0, LexerError{Token: g.Name, Line: 10, Column: total} }
	return total, nil
}
func GeneratedFunction010() int {
	generated := NewGeneratedStruct010[int]("GeneratedStruct010", 1, 2, 3)
	generated.Push(10)
	value, err := generated.Nested(10)
	if err != nil { return -10 }
	return value + len(generated.Values)
}
type GeneratedStruct011[T comparable] struct {
	ID       int
	Name     string
	Values   []T
	Index    map[T]int
	Metadata map[string]any
}
func NewGeneratedStruct011[T comparable](name string, values ...T) *GeneratedStruct011[T] {
	instance := &GeneratedStruct011[T]{ID: 11, Name: name, Values: make([]T, 0, len(values)), Index: map[T]int{}, Metadata: map[string]any{"block": 11, "name": name}}
	for _, value := range values { instance.Push(value) }
	return instance
}
func (g *GeneratedStruct011[T]) Push(value T) {
	g.Index[value] = len(g.Values)
	g.Values = append(g.Values, value)
}
func (g *GeneratedStruct011[T]) Nested(input int) (int, error) {
	total := 0
GeneratedLoop011:
	for outer := 0; outer < 3; outer++ {
		for inner := 0; inner < 3; inner++ {
			switch {
			case (outer+inner+input+11)%2 == 0:
				total += outer*inner + 11
			case inner == 1:
				total += input
			default:
				continue GeneratedLoop011
			}
		}
	}
	if total < 0 { return 0, LexerError{Token: g.Name, Line: 11, Column: total} }
	return total, nil
}
func GeneratedFunction011() int {
	generated := NewGeneratedStruct011[int]("GeneratedStruct011", 1, 2, 3)
	generated.Push(11)
	value, err := generated.Nested(11)
	if err != nil { return -11 }
	return value + len(generated.Values)
}
type GeneratedStruct012[T comparable] struct {
	ID       int
	Name     string
	Values   []T
	Index    map[T]int
	Metadata map[string]any
}
func NewGeneratedStruct012[T comparable](name string, values ...T) *GeneratedStruct012[T] {
	instance := &GeneratedStruct012[T]{ID: 12, Name: name, Values: make([]T, 0, len(values)), Index: map[T]int{}, Metadata: map[string]any{"block": 12, "name": name}}
	for _, value := range values { instance.Push(value) }
	return instance
}
func (g *GeneratedStruct012[T]) Push(value T) {
	g.Index[value] = len(g.Values)
	g.Values = append(g.Values, value)
}
func (g *GeneratedStruct012[T]) Nested(input int) (int, error) {
	total := 0
GeneratedLoop012:
	for outer := 0; outer < 3; outer++ {
		for inner := 0; inner < 3; inner++ {
			switch {
			case (outer+inner+input+12)%2 == 0:
				total += outer*inner + 12
			case inner == 1:
				total += input
			default:
				continue GeneratedLoop012
			}
		}
	}
	if total < 0 { return 0, LexerError{Token: g.Name, Line: 12, Column: total} }
	return total, nil
}
func GeneratedFunction012() int {
	generated := NewGeneratedStruct012[int]("GeneratedStruct012", 1, 2, 3)
	generated.Push(12)
	value, err := generated.Nested(12)
	if err != nil { return -12 }
	return value + len(generated.Values)
}
type GeneratedStruct013[T comparable] struct {
	ID       int
	Name     string
	Values   []T
	Index    map[T]int
	Metadata map[string]any
}
func NewGeneratedStruct013[T comparable](name string, values ...T) *GeneratedStruct013[T] {
	instance := &GeneratedStruct013[T]{ID: 13, Name: name, Values: make([]T, 0, len(values)), Index: map[T]int{}, Metadata: map[string]any{"block": 13, "name": name}}
	for _, value := range values { instance.Push(value) }
	return instance
}
func (g *GeneratedStruct013[T]) Push(value T) {
	g.Index[value] = len(g.Values)
	g.Values = append(g.Values, value)
}
func (g *GeneratedStruct013[T]) Nested(input int) (int, error) {
	total := 0
GeneratedLoop013:
	for outer := 0; outer < 3; outer++ {
		for inner := 0; inner < 3; inner++ {
			switch {
			case (outer+inner+input+13)%2 == 0:
				total += outer*inner + 13
			case inner == 1:
				total += input
			default:
				continue GeneratedLoop013
			}
		}
	}
	if total < 0 { return 0, LexerError{Token: g.Name, Line: 13, Column: total} }
	return total, nil
}
func GeneratedFunction013() int {
	generated := NewGeneratedStruct013[int]("GeneratedStruct013", 1, 2, 3)
	generated.Push(13)
	value, err := generated.Nested(13)
	if err != nil { return -13 }
	return value + len(generated.Values)
}
type GeneratedStruct014[T comparable] struct {
	ID       int
	Name     string
	Values   []T
	Index    map[T]int
	Metadata map[string]any
}
func NewGeneratedStruct014[T comparable](name string, values ...T) *GeneratedStruct014[T] {
	instance := &GeneratedStruct014[T]{ID: 14, Name: name, Values: make([]T, 0, len(values)), Index: map[T]int{}, Metadata: map[string]any{"block": 14, "name": name}}
	for _, value := range values { instance.Push(value) }
	return instance
}
func (g *GeneratedStruct014[T]) Push(value T) {
	g.Index[value] = len(g.Values)
	g.Values = append(g.Values, value)
}
func (g *GeneratedStruct014[T]) Nested(input int) (int, error) {
	total := 0
GeneratedLoop014:
	for outer := 0; outer < 3; outer++ {
		for inner := 0; inner < 3; inner++ {
			switch {
			case (outer+inner+input+14)%2 == 0:
				total += outer*inner + 14
			case inner == 1:
				total += input
			default:
				continue GeneratedLoop014
			}
		}
	}
	if total < 0 { return 0, LexerError{Token: g.Name, Line: 14, Column: total} }
	return total, nil
}
func GeneratedFunction014() int {
	generated := NewGeneratedStruct014[int]("GeneratedStruct014", 1, 2, 3)
	generated.Push(14)
	value, err := generated.Nested(14)
	if err != nil { return -14 }
	return value + len(generated.Values)
}
type GeneratedStruct015[T comparable] struct {
	ID       int
	Name     string
	Values   []T
	Index    map[T]int
	Metadata map[string]any
}
func NewGeneratedStruct015[T comparable](name string, values ...T) *GeneratedStruct015[T] {
	instance := &GeneratedStruct015[T]{ID: 15, Name: name, Values: make([]T, 0, len(values)), Index: map[T]int{}, Metadata: map[string]any{"block": 15, "name": name}}
	for _, value := range values { instance.Push(value) }
	return instance
}
func (g *GeneratedStruct015[T]) Push(value T) {
	g.Index[value] = len(g.Values)
	g.Values = append(g.Values, value)
}
func (g *GeneratedStruct015[T]) Nested(input int) (int, error) {
	total := 0
GeneratedLoop015:
	for outer := 0; outer < 3; outer++ {
		for inner := 0; inner < 3; inner++ {
			switch {
			case (outer+inner+input+15)%2 == 0:
				total += outer*inner + 15
			case inner == 1:
				total += input
			default:
				continue GeneratedLoop015
			}
		}
	}
	if total < 0 { return 0, LexerError{Token: g.Name, Line: 15, Column: total} }
	return total, nil
}
func GeneratedFunction015() int {
	generated := NewGeneratedStruct015[int]("GeneratedStruct015", 1, 2, 3)
	generated.Push(15)
	value, err := generated.Nested(15)
	if err != nil { return -15 }
	return value + len(generated.Values)
}
type GeneratedStruct016[T comparable] struct {
	ID       int
	Name     string
	Values   []T
	Index    map[T]int
	Metadata map[string]any
}
func NewGeneratedStruct016[T comparable](name string, values ...T) *GeneratedStruct016[T] {
	instance := &GeneratedStruct016[T]{ID: 16, Name: name, Values: make([]T, 0, len(values)), Index: map[T]int{}, Metadata: map[string]any{"block": 16, "name": name}}
	for _, value := range values { instance.Push(value) }
	return instance
}
func (g *GeneratedStruct016[T]) Push(value T) {
	g.Index[value] = len(g.Values)
	g.Values = append(g.Values, value)
}
func (g *GeneratedStruct016[T]) Nested(input int) (int, error) {
	total := 0
GeneratedLoop016:
	for outer := 0; outer < 3; outer++ {
		for inner := 0; inner < 3; inner++ {
			switch {
			case (outer+inner+input+16)%2 == 0:
				total += outer*inner + 16
			case inner == 1:
				total += input
			default:
				continue GeneratedLoop016
			}
		}
	}
	if total < 0 { return 0, LexerError{Token: g.Name, Line: 16, Column: total} }
	return total, nil
}
func GeneratedFunction016() int {
	generated := NewGeneratedStruct016[int]("GeneratedStruct016", 1, 2, 3)
	generated.Push(16)
	value, err := generated.Nested(16)
	if err != nil { return -16 }
	return value + len(generated.Values)
}
type GeneratedStruct017[T comparable] struct {
	ID       int
	Name     string
	Values   []T
	Index    map[T]int
	Metadata map[string]any
}
func NewGeneratedStruct017[T comparable](name string, values ...T) *GeneratedStruct017[T] {
	instance := &GeneratedStruct017[T]{ID: 17, Name: name, Values: make([]T, 0, len(values)), Index: map[T]int{}, Metadata: map[string]any{"block": 17, "name": name}}
	for _, value := range values { instance.Push(value) }
	return instance
}
func (g *GeneratedStruct017[T]) Push(value T) {
	g.Index[value] = len(g.Values)
	g.Values = append(g.Values, value)
}
}
