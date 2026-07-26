#define Uses_TKeys
#include <tvision/tv.h>

#include <internal/termio.h>
#include <internal/far2l.h>
#include <internal/conctl.h>
#include <internal/win32con.h>
#include <internal/getenv.h>
#include <internal/base64.h>

#include <chrono>

namespace tvision
{

void GetChBuf::reject() noexcept
{
    while (size)
        unget();
}

// getNum, getInt: INVARIANT: the last non-digit read key (or -1)
// can be accessed with 'last()' and can also be ungetted.

bool GetChBuf::getNum(uint &result) noexcept
{
    uint num = 0, digits = 0;
    int k;
    while ((k = get(true)) != -1 && '0' <= k && k <= '9')
    {
        num = 10 * num + (k - '0');
        ++digits;
    }
    if (digits)
        return (result = num), true;
    return false;
}

bool GetChBuf::getInt(int &result) noexcept
{
    int num = 0, digits = 0, sign = 1;
    int k = get(true);
    if (k == '-')
    {
        sign = -1;
        k = get(true);
    }
    while (k != -1 && '0' <= k && k <= '9')
    {
        num = 10 * num + (k - '0');
        ++digits;
        k = get(true);
    }
    if (digits)
        return (result = sign*num), true;
    return false;
}

bool GetChBuf::readStr(TStringView str) noexcept
{
    size_t origSize = size;
    size_t i = 0;
    while (i < str.size() && get() == str[i])
        ++i;
    if (i == str.size())
        return true;
    while (origSize < size)
        unget();
    return false;
}

bool CSIData::readFrom(GetChBuf &buf) noexcept
// Pre: "\x1B[" has just been read.
{
    int first = buf.get();
    if (first == '?' || first == '>' || first == '<' || first == '=')
        prefix = (char) first;
    else
        buf.unget();

    for (uint i = 0; i < maxLength; ++i)
    {
        if (!buf.getNum(_values[i]))
            _values[i] = UINT_MAX;
        int k = buf.last();
        if (k == -1)
            // No more input and CSI is not yet complete.
            return false;
        if (k == ';' || k == ':')
            _separators[i] = (char) k;
        else
        {
            terminator = (char) k;
            length = i + 1;
            return true;
        }
    }
    // CSI may be longer than supported.
    return false;
}

// The default mouse experience with Ncurses is not always good. To work around
// some issues, we request and parse mouse events manually.

void TermIO::mouseOn(ConsoleCtl &con) noexcept
{
    TStringView seq = "\x1B[?1001s" // Save old highlight mouse reporting.
                      "\x1B[?1000h" // Enable mouse reporting.
                      "\x1B[?1002h" // Enable mouse drag reporting.
                      "\x1B[?1003h" // Enable mouse move reporting.
                      "\x1B[?1006h" // Enable SGR extended mouse reporting.
                    ;
    con.write(seq.data(), seq.size());
}

void TermIO::mouseOff(ConsoleCtl &con) noexcept
{
    TStringView seq = "\x1B[?1006l" // Disable SGR extended mouse reporting.
                      "\x1B[?1003l" // Disable mouse move reporting.
                      "\x1B[?1002l" // Disable mouse drag reporting.
                      "\x1B[?1000l" // Disable mouse reporting.
                      "\x1B[?1001r" // Restore old highlight mouse reporting.
                    ;
    con.write(seq.data(), seq.size());
}

static const uint kittyKeyboardFlags = 0x1F;

void TermIO::keyModsOn(ConsoleCtl &con, InputState &state) noexcept
{
    char buf[256];

    state.kittyKeyboard = {};
    state.kittyKeyboard.phase = kkpQuerying;
    state.kittyKeyboard.stateEventPending = true;

    strcpy(buf,
        "\x1B[?1036s"   // Save metaSendsEscape (XTerm).
        "\x1B[?1036h"   // Enable metaSendsEscape (XTerm).
        "\x1B[?2004s"   // Save bracketed paste.
        "\x1B[?2004h"   // Enable bracketed paste.
        "\x1B[>4;1m"    // Enable modifyOtherKeys (XTerm).
        "\x1B[?9001h"   // Enable win32-input-mode (Conpty).
        far2lEnableSeq  // Enable far2l terminal extensions.
    );

    if (char *term = getenv("TERM"))
    {
        // Check for full OSC 52 clipboard support.
        if (strstr(term, "alacritty") || strstr(term, "foot"))
            strcat(buf,
                // Request clipboard contents to see if they are readable. It is
                // not safe to print this blindly so only do it for TERMs which
                // we know should work.
                "\x1B]52;;?\x07"
            );
        else
            strcat(buf,
                // Check for the 'kitty-query-clipboard_control' capability (XTGETTCAP).
                "\x1BP+q6b697474792d71756572792d636c6970626f6172645f636f6e74726f6c\x1B\\"
                // Check for 'allowWindowOps' (XTQALLOWED).
                "\x1B]60\x1B\\"
            );
    }

    strcat(buf,
        // Query Kitty keyboard protocol support, followed by primary device
        // attributes as the response boundary required by the protocol.
        "\x1B[?u"
        "\x1B[c"
        // Some terminals do not recognize the sequences above and will display
        // them on screen. Clear the screen to prevent this.
        "\x1B[2J"
    );

    con.write(buf, strlen(buf));
}

void TermIO::keyModsOff(ConsoleCtl &con, InputState &state) noexcept
{
    if (state.kittyKeyboard.pushed)
    {
        TStringView kittySeq = "\x1B[<u";
        con.write(kittySeq.data(), kittySeq.size());
    }

    TStringView seq = far2lDisableSeq
                      "\x1B[?9001l" // Disable win32-input-mode (Conpty).
                      "\x1B[>4m"    // Reset modifyOtherKeys (XTerm).
                      "\x1B[?2004l" // Disable bracketed paste.
                      "\x1B[?2004r" // Restore bracketed paste.
                      "\x1B[?1036r" // Restore metaSendsEscape (XTerm).
                    ;
    con.write(seq.data(), seq.size());
    state.kittyKeyboard = {};
}

void TermIO::applyPendingKeyMods(ConsoleCtl &con, InputState &state) noexcept
{
    KittyKeyboardState &kitty = state.kittyKeyboard;

    if (kitty.phase == kkpEnablePending)
    {
        TStringView seq = "\x1B[>31u" // Push all required progressive enhancements.
                          "\x1B[?u"   // Verify the resulting flags.
                          "\x1B[c"    // Response boundary.
                        ;
        con.write(seq.data(), seq.size());
        kitty.reportedFlags = 0;
        kitty.queryAnswered = false;
        kitty.pushed = true;
        kitty.phase = kkpVerifying;
    }
    else if (kitty.phase == kkpDisablePending)
    {
        if (kitty.pushed)
        {
            TStringView seq = "\x1B[<u";
            con.write(seq.data(), seq.size());
        }
        kitty = {};
    }
}

static ParseResult parseKittyControlResponse(const CSIData &csi, InputState &state) noexcept
{
    if (csi.prefix != '?')
        return Rejected;

    KittyKeyboardState &kitty = state.kittyKeyboard;
    if (csi.terminator == 'u')
    {
        if (kitty.phase == kkpQuerying || kitty.phase == kkpVerifying)
        {
            kitty.reportedFlags = csi.getValue(0, 0);
            kitty.queryAnswered = true;
        }
        return Ignored;
    }
    if (csi.terminator == 'c')
    {
        if (kitty.phase == kkpQuerying)
            kitty.phase = kitty.queryAnswered ? kkpEnablePending : kkpDisabled;
        else if (kitty.phase == kkpVerifying)
        {
            if (kitty.queryAnswered &&
                (kitty.reportedFlags & kittyKeyboardFlags) == kittyKeyboardFlags)
            {
                kitty.phase = kkpActive;
                kitty.stateEventPending = true;
            }
            else
                kitty.phase = kkpDisablePending;
        }
        return Ignored;
    }
    return Rejected;
}

ParseResult TermIO::parseEscapeSeq(GetChBuf &buf, TEvent &ev, InputState &state) noexcept
// Pre: "\x1B" has just been read.
{
    ParseResult res = Rejected;
    switch (buf.get())
    {
        case '_':
            if (buf.readStr("f2l"))
                return parseFar2lInput(buf, ev, state);
            if (buf.readStr("far2l"))
                return parseFar2lAnswer(buf, ev, state);
            break;
        case '[':
            switch (buf.get())
            {
                // Note: mouse events are usually detected in 'NcursesInput::parseCursesMouse'.
                case 'M':
                    return parseX10Mouse(buf, ev, state) == Accepted ? Accepted : Ignored;
                case '<':
                    return parseSGRMouse(buf, ev, state) == Accepted ? Accepted : Ignored;
                default:
                {
                    buf.unget();
                    CSIData csi;
                    if (csi.readFrom(buf))
                    {
                        ParseResult kittyControl = parseKittyControlResponse(csi, state);
                        if (kittyControl != Rejected)
                            return kittyControl;
                        switch (csi.terminator)
                        {
                            case 'u':
                                return parseKittyKey(csi, ev, state);
                            case 'R':
                                return parseCPR(csi, state);
                            case '_':
                                return parseWin32InputModeKeyOrEscapeSeq(csi, buf.in, ev, state);
                            default:
                                return parseCSIKey(csi, ev, state);
                        }
                    }
                    break;
                }
            }
            break;
        case 'O':
            return parseSS3Key(buf, ev);
        case 'P':
            return parseDCS(buf, state);
        case ']':
            return parseOSC(buf, state);
        case '\x1B':
            res = parseEscapeSeq(buf, ev, state);
            if (res == Accepted && ev.what == evKeyDown)
            {
                ev.keyDown.controlKeyState |= kbLeftAlt;
                normalizeKey(ev.keyDown);
            }
            break;
    }
    return res;
}

const ushort
    mmAlt = 0x08,
    mmCtrl = 0x10;

ParseResult TermIO::parseX10Mouse(GetChBuf &buf, TEvent &ev, InputState &state) noexcept
// Pre: "\x1B[M" has just been read.
// The complete sequence looks like "\x1B[Mabc", where:
// * 'a' is the button number plus 32.
// * 'b' is the column number (one-based) plus 32.
// * 'c' is the row number (one-based) plus 32.
{
    uint butm = (uint) buf.get();
    uint mod = butm & (mmAlt | mmCtrl);
    uint but = (butm & ~(mmAlt | mmCtrl)) - 32;
    if (255 - 32 < but) return Rejected;
    int col, row;
    for (int *i : {&col, &row})
    {
        *i = buf.get();
        if (*i < 0 || 255 < *i)
            return Rejected;
        // In theory, this encoding only supports coordinates in the range [0, 222].
        // However, some terminal emulators (e.g. urxvt) keep increasing the
        // counters, causing an overflow. We can take advantage of this to support
        // more coordinates, but we definitely don't want to reject the sequence,
        // as that will cause Ctrl+key events to be generated.
        if (*i > 32)
            *i -= 32;
        else
            *i += (256 - 32);
        // Make it zero-based.
        --*i;
    }

    ev.what = evMouse;
    ev.mouse = {};
    ev.mouse.where = {col, row};
    ev.mouse.controlKeyState = (-!!(mod & mmAlt) & kbLeftAlt) | (-!!(mod & mmCtrl) & kbLeftCtrl);
    switch (but)
    {
        case 0: // Press.
        case 32: // Drag.
            state.buttons |= mbLeftButton; break;
        case 1:
        case 33:
            state.buttons |= mbMiddleButton; break;
        case 2:
        case 34:
            state.buttons |= mbRightButton; break;
        case 3: state.buttons = 0; break; // Release.
        case 64: ev.mouse.wheel = mwUp; break;
        case 65: ev.mouse.wheel = mwDown; break;
    }
    ev.mouse.buttons = state.buttons;
    return Accepted;
}

ParseResult TermIO::parseSGRMouse(GetChBuf &buf, TEvent &ev, InputState &state) noexcept
// https://invisible-island.net/xterm/ctlseqs/ctlseqs.html#h3-Extended-coordinates
// Pre: "\x1B[<" has just been read.
// The complete sequence looks like "\x1B[<a;b;cM" or "\x1B[<a;b;cm", where:
// * 'a' is a sequence of digits representing the button number in decimal.
// * 'b' is a sequence of digits representing the column number (one-based) in decimal.
// * 'c' is a sequence of digits representing the row number (one-based) in decimal.
// The sequence ends with 'M' on button press and on 'm' on button release.
{
    uint butm;
    if (!buf.getNum(butm)) return Rejected;
    uint mod = butm & (mmAlt | mmCtrl);
    uint but = butm & ~(mmAlt | mmCtrl);
    // IntelliJ may emit negative coordinates.
    int col, row;
    if (!buf.getInt(col) || !buf.getInt(row)) return Rejected;
    // Make the coordinates zero-based.
    row = max(row, 1);
    col = max(col, 1);
    --row, --col;
    // Finally, the press/release state.
    uint type = (uint) buf.last();
    if (!(type == 'M' || type == 'm')) return Rejected;

    ev.what = evMouse;
    ev.mouse = {};
    ev.mouse.where = {col, row};
    ev.mouse.controlKeyState = (-!!(mod & mmAlt) & kbLeftAlt) | (-!!(mod & mmCtrl) & kbLeftCtrl);
    if (type == 'M') // Press, wheel or drag.
    {
        switch (but)
        {
            case 0:
            case 32:
                state.buttons |= mbLeftButton; break;
            case 1:
            case 33:
                state.buttons |= mbMiddleButton; break;
            case 2:
            case 34:
                state.buttons |= mbRightButton; break;
            case 64: ev.mouse.wheel = mwUp; break;
            case 65: ev.mouse.wheel = mwDown; break;
        }
    }
    else // Release.
    {
        switch (but)
        {
            case 0: state.buttons &= ~mbLeftButton; break;
            case 1: state.buttons &= ~mbMiddleButton; break;
            case 2: state.buttons &= ~mbRightButton; break;
        }
    }
    ev.mouse.buttons = state.buttons;
    return Accepted;
}

ParseResult TermIO::parseDCS(GetChBuf &buf, InputState &state) noexcept
// Pre: '\x1BP' has just been read.
{
    if (char *s = readUntilBelOrSt(buf))
    {
        // We only get a DCS in response to our request for kitty capabilities.
        if (strstr(s, "726561642d636c6970626f617264")) // 'read-clipboard'
            state.hasFullOsc52 = true;
        free(s);
    }
    return Ignored;
}

ParseResult TermIO::parseOSC(GetChBuf &buf, InputState &state) noexcept
// Pre: '\x1B]' has just been read.
{
    if (char *s = readUntilBelOrSt(buf))
    {
        TStringView sv(s);
        if (sv.size() > 3 && sv.substr(0, 3) == "52;") // OSC 52
        {
            if (char *begin = (char *) memchr(&sv[3], ';', sv.size() - 3))
            {
                if (!state.hasFullOsc52)
                    // We got a response to our initial request.
                    state.hasFullOsc52 = true;
                else if (state.putPaste)
                {
                    TStringView encoded = sv.substr(begin + 1 - &sv[0]);
                    if (char *pDecoded = (char *) malloc((encoded.size() * 3)/4 + 3))
                    {
                        TStringView decoded = decodeBase64(encoded, pDecoded);
                        state.putPaste(decoded);
                        free(pDecoded);
                    }
                }
            }
        }
        else if (sv.size() > 3 && sv.substr(0, 3) == "60;") // OSC 60
            if (strstr(&sv[3], "allowWindowOps"))
                state.hasFullOsc52 = true;
        free(s);
    }
    return Ignored;
}

ParseResult TermIO::parseCPR(const CSIData &csi, InputState &state) noexcept
// Pre: csi.terminator == 'R'.
// We receive a Cursor Position Report as response to the Device Status Report
// request we make in 'consumeUnprocessedInput()'.
{
    if (csi.length != 2 || csi.getSeparator(0) != ';')
        return Rejected;

    state.gotDsrResponse = true;
    return Ignored;
}

static ParseResult parseWin32InputModeKey(const CSIData &csi, TEvent &ev, InputState &state) noexcept
// https://github.com/microsoft/terminal/blob/main/doc/specs/%234999%20-%20Improved%20keyboard%20handling%20in%20Conpty.md
{
    KEY_EVENT_RECORD kev;
    kev.wVirtualKeyCode = (ushort) csi.getValue(0, 0);
    kev.wVirtualScanCode = (ushort) csi.getValue(1, 0);
    kev.uChar.UnicodeChar = (ushort) csi.getValue(2, 0);
    kev.bKeyDown = (ushort) csi.getValue(3, 0);
    kev.dwControlKeyState = (ushort) csi.getValue(4, 0);
    kev.wRepeatCount = (ushort) csi.getValue(5, 1);

    regenerateMissingScanCodeFromVirtualKeyCode(kev);

    if (kev.bKeyDown && getWin32Key(kev, ev, state))
    {
        TermIO::normalizeKey(ev.keyDown);
        return Accepted;
    }
    return Ignored;
}

// Due to issue https://github.com/microsoft/terminal/issues/15083, Conpty will
// emit ANSI escape sequences wrapped in win32-input-mode events. This class
// allows handling these sequences properly.

class Win32InputModeUnwrapper : public InputGetter
{
    InputGetter &in;
    InputState &state;

    enum { maxSize = 31 };

    ushort ungetSize {0};
    short ungetBuffer[maxSize];

public:

    Win32InputModeUnwrapper(InputGetter &aIn, InputState &aState) noexcept :
        in(aIn), state(aState)
    {
    }

    int get() noexcept override
    {
        if (ungetSize > 0)
            return ungetBuffer[--ungetSize];

        GetChBuf buf(in);
        CSIData csi;
        TEvent ev {};
        // If we get a win32-input-mode event with no scan code and
        // a single-byte character, take just that character.
        if ( buf.get() == '\x1B' && buf.get() == '['
             && csi.readFrom(buf) && csi.terminator == '_'
             && parseWin32InputModeKey(csi, ev, state) == Accepted
             && ev.keyDown.charScan.scanCode == 0
             && ev.keyDown.textLength == 1 )
            return (uchar) ev.keyDown.text[0];
        buf.reject();
        return -1;
    }

    void unget(int key) noexcept override
    {
        // We could reconstruct the original win32-input-mode event and call
        // 'in.unget()', but there is no need for that. However, we still need
        // to be able to temporarily store characters returned by 'get()'.
        if (ungetSize < maxSize)
            ungetBuffer[ungetSize++] = (short) key;
    }
};

ParseResult TermIO::parseWin32InputModeKeyOrEscapeSeq(const CSIData &csi, InputGetter &in, TEvent &ev, InputState &state) noexcept
// Pre: csi.terminator == '_'.
{
    ParseResult res = parseWin32InputModeKey(csi, ev, state);
    if (res == Accepted && ev.keyDown == 0x001B)
    {
        // We received the initiator of an escape sequence wrapped in
        // win32-input-mode events.
        Win32InputModeUnwrapper unwrapper(in, state);
        GetChBuf buf(unwrapper);
        res = parseEscapeSeq(buf, ev, state);
        // Avoid propagating 'Rejected' because we have used a secondary GetChBuf.
        if (res != Accepted)
            res = Ignored;
    }
    return res;
}

static bool setOsc52Clipboard(ConsoleCtl &con, TStringView text, InputState &state) noexcept
{
    TStringView prefix = "\x1B]52;;";
    TStringView suffix = "\x07";
    if (char *buf = (char *) malloc(prefix.size() + suffix.size() + (text.size() * 4)/3 + 4))
    {
        memcpy(buf, prefix.data(), prefix.size());
        TStringView b64 = encodeBase64(text, buf + prefix.size());
        memcpy(buf + prefix.size() + b64.size(), suffix.data(), suffix.size());
        con.write(buf, prefix.size() + b64.size() + suffix.size());
        free(buf);
    }
    // Return false when there is no full OSC 52 support, even though we always
    // make the request. This way, we can still use the internal clipboard.
    return state.hasFullOsc52;
}

static bool requestOsc52Clipboard(ConsoleCtl &con, InputState &state) noexcept
{
    if (state.hasFullOsc52)
    {
        TStringView seq = "\x1B]52;;?\x07";
        con.write(seq.data(), seq.size());
        return true;
    }
    return false;
}

bool TermIO::setClipboardText(ConsoleCtl &con, TStringView text, InputState &state) noexcept
{
    return setFar2lClipboard(con, text, state)
        || setOsc52Clipboard(con, text, state);
}

bool TermIO::requestClipboardText(ConsoleCtl &con, void (&accept)(TStringView), InputState &state) noexcept
{
    state.putPaste = &accept;
    return requestFar2lClipboard(con, state)
        || requestOsc52Clipboard(con, state);
}

char *TermIO::readUntilBelOrSt(GetChBuf &buf) noexcept
// Returns a malloc-allocated and null-terminated string, or null.
{
    size_t capacity = 1024;
    size_t len = 0;
    if (char *s = (char *) malloc(capacity))
    {
        int prev = '\0';
        int c;
        while (c = buf.getUnbuffered(), c != -1)
        {
            if (c == '\x07') // BEL
                break;
            if (c == '\\' && prev == '\x1B') // ST
            {
                len -= (len > 0);
                break;
            }
            if (capacity == len + 1)
            {
                if (void *tmp = realloc(s, capacity *= 2))
                    s = (char *) tmp;
                else
                    s = (free(s), nullptr);
            }
            if (s)
                s[len++] = (char) c;
            prev = c;
        }
        if (s)
            s[len] = '\0';
        return s;
    }
    return {};
}

void TermIO::consumeUnprocessedInput(ConsoleCtl &con, InputGetter &in, InputState &state) noexcept
// The terminal might have kept sending us events while the application is
// exiting. This is especially likely to happen when the application is running
// remotely accross a slow connection and terminal extensions are in place
// which report key release events (e.g. far2l and win32-input-mode), or when
// the application gets killed by a signal while the user was dragging the mouse.
// Therefore, we print a DSR request and attempt to read events until we get a
// response to it. This has to be done after disabling keyboard and mouse extensions.
{
    using namespace std::chrono;
    auto timeout = milliseconds(200);

    TStringView seq = "\x1B[6n"; // Device Status Report.
    con.write(seq.data(), seq.size());

    TEvent ev {};
    state.gotDsrResponse = false;
    auto begin = steady_clock::now();
    do
    {
        GetChBuf buf {in};
        parseEvent(buf, ev, state);
    }
    while ( !state.gotDsrResponse &&
            (steady_clock::now() - begin <= timeout) );
}

} // namespace tvision
