#define Uses_TKeys
#include <tvision/tv.h>

#include <internal/termio.h>
#include <internal/constmap.h>
#include <internal/constarr.h>
#include <internal/codepage.h>
#include <internal/utf8.h>

namespace tvision
{

static_assert(KittyKeyboardState::maxPendingText >= CSIData::maxLength - 4, "");

static const const_unordered_map<ushort, constarray<ushort, 3>> moddedKeyCodes =
{
    { 'A', {0, kbCtrlA, kbAltA}                }, { 'B', {0, kbCtrlB, kbAltB}                },
    { 'C', {0, kbCtrlC, kbAltC}                }, { 'D', {0, kbCtrlD, kbAltD}                },
    { 'E', {0, kbCtrlE, kbAltE}                }, { 'F', {0, kbCtrlF, kbAltF}                },
    { 'G', {0, kbCtrlG, kbAltG}                }, { 'H', {0, kbCtrlH, kbAltH}                },
    { 'I', {0, kbCtrlI, kbAltI}                }, { 'J', {0, kbCtrlJ, kbAltJ}                },
    { 'K', {0, kbCtrlK, kbAltK}                }, { 'L', {0, kbCtrlL, kbAltL}                },
    { 'M', {0, kbCtrlM, kbAltM}                }, { 'N', {0, kbCtrlN, kbAltN}                },
    { 'O', {0, kbCtrlO, kbAltO}                }, { 'P', {0, kbCtrlP, kbAltP}                },
    { 'Q', {0, kbCtrlQ, kbAltQ}                }, { 'R', {0, kbCtrlR, kbAltR}                },
    { 'S', {0, kbCtrlS, kbAltS}                }, { 'T', {0, kbCtrlT, kbAltT}                },
    { 'U', {0, kbCtrlU, kbAltU}                }, { 'V', {0, kbCtrlV, kbAltV}                },
    { 'W', {0, kbCtrlW, kbAltW}                }, { 'X', {0, kbCtrlX, kbAltX}                },
    { 'Y', {0, kbCtrlY, kbAltY}                }, { 'Z', {0, kbCtrlZ, kbAltZ}                },
    { '1', {0, 0, kbAlt1}                      }, { '2', {0, 0, kbAlt2}                      },
    { '3', {0, 0, kbAlt3}                      }, { '4', {0, 0, kbAlt4}                      },
    { '5', {0, 0, kbAlt5}                      }, { '6', {0, 0, kbAlt6}                      },
    { '7', {0, 0, kbAlt7}                      }, { '8', {0, 0, kbAlt8}                      },
    { '9', {0, 0, kbAlt9}                      }, { '0', {0, 0, kbAlt0}                      },
    { ' ', {0, 0, kbAltSpace}                  }, { '-', {0, 0, kbAltMinus}                  },
    { '=', {0, 0, kbAltEqual}                  },
    { kbF1, {kbShiftF1, kbCtrlF1, kbAltF1}     }, { kbF2, {kbShiftF2, kbCtrlF2, kbAltF2}     },
    { kbF3, {kbShiftF3, kbCtrlF3, kbAltF3}     }, { kbF4, {kbShiftF4, kbCtrlF4, kbAltF4}     },
    { kbF5, {kbShiftF5, kbCtrlF5, kbAltF5}     }, { kbF6, {kbShiftF6, kbCtrlF6, kbAltF6}     },
    { kbF7, {kbShiftF7, kbCtrlF7, kbAltF7}     }, { kbF8, {kbShiftF8, kbCtrlF8, kbAltF8}     },
    { kbF9, {kbShiftF9, kbCtrlF9, kbAltF9}     }, { kbF10, {kbShiftF10, kbCtrlF10, kbAltF10} },
    { kbF11, {kbShiftF11, kbCtrlF11, kbAltF11} }, { kbF12, {kbShiftF12, kbCtrlF12, kbAltF12} },
    { kbEsc, {0, 0, kbAltEsc}                  }, { kbBack, {0, kbCtrlBack, kbAltBack}       },
    { kbTab, {kbShiftTab, kbCtrlTab, kbAltTab} }, { kbEnter, {0, kbCtrlEnter, kbAltEnter}    },
    { kbHome, {0, kbCtrlHome, kbAltHome}       }, { kbUp, {0, kbCtrlUp, kbAltUp}             },
    { kbPgUp, {0, kbCtrlPgUp, kbAltPgUp}       }, { kbLeft, {0, kbCtrlLeft, kbAltLeft}       },
    { kbRight, {0, kbCtrlRight, kbAltRight}    }, { kbEnd, {0, kbCtrlEnd, kbAltEnd}          },
    { kbDown, {0, kbCtrlDown, kbAltDown}       }, { kbPgDn, {0, kbCtrlPgDn, kbAltPgDn}       },
    { kbIns, {kbShiftIns, kbCtrlIns, kbAltIns} }, { kbDel, {kbShiftDel, kbCtrlDel, kbAltDel} },
};

static ushort getModdedKeyCode(ushort keyCode, ushort tvMods)
{
    // Modifier precedece: Shift < Ctrl < Alt.
    int largestMod = (tvMods & kbLeftAlt) ? 2
                   : (tvMods & kbLeftCtrl) ? 1
                   : 0;
    return moddedKeyCodes[keyCode][largestMod];
}

const uint XTermModDefault = 1;

static ushort modsFromXTerm(uint mods) noexcept
{
    if (mods < XTermModDefault)
        return 0;
    mods -= XTermModDefault;
    return
          (kbShift & -(mods & 1))
        | (kbLeftAlt & -(mods & 2))
        | (kbLeftCtrl & -(mods & 4))
        | (kbLeftSuper & -(mods & 8))
        ;
}

static KeyDownEvent keyWithXTermMods(ushort keyCode, uint mods) noexcept
{
    ushort tvMods = modsFromXTerm(mods);
    KeyDownEvent keyDown {{keyCode}, tvMods};
    TermIO::normalizeKey(keyDown);
    return keyDown;
}

static bool isAlpha(uint32_t ascii) noexcept
{
    return ' ' <= ascii && ascii < 127;
};

static bool isAsciiLetter(uint32_t ch) noexcept
{
    return ('A' <= ch && ch <= 'Z') || ('a' <= ch && ch <= 'z');
};

static bool isPrivate(uint32_t codepoint) noexcept
{
    return 57344 <= codepoint && codepoint <= 63743;
};

static bool keyFromCodepoint(uint value, uint mods, KeyDownEvent &keyDown) noexcept
{
    ushort keyCode = 0;
    switch (value)
    {
        case     8: keyCode = kbBack;   break;
        case     9: keyCode = kbTab;    break;
        case    13: keyCode = kbEnter;  break;
        case    27: keyCode = kbEsc;    break;
        case   127: keyCode = kbBack;   break;
        // Functional keys as represented in Kitty's keyboard protocol.
        // https://sw.kovidgoyal.net/kitty/keyboard-protocol.html#functional
        // Keypad.
        case 57399: keyCode = '0';      break;
        case 57400: keyCode = '1';      break;
        case 57401: keyCode = '2';      break;
        case 57402: keyCode = '3';      break;
        case 57403: keyCode = '4';      break;
        case 57404: keyCode = '5';      break;
        case 57405: keyCode = '6';      break;
        case 57406: keyCode = '7';      break;
        case 57407: keyCode = '8';      break;
        case 57408: keyCode = '9';      break;
        case 57409: keyCode = '.';      break;
        case 57410: keyCode = '/';      break;
        case 57411: keyCode = '*';      break;
        case 57412: keyCode = '-';      break;
        case 57413: keyCode = '+';      break;
        case 57414: keyCode = kbEnter;  break;
        case 57415: keyCode = '=';      break;
        case 57416: keyCode = ',';      break;
        case 57417: keyCode = kbLeft;   break;
        case 57418: keyCode = kbRight;  break;
        case 57419: keyCode = kbUp;     break;
        case 57420: keyCode = kbDown;   break;
        case 57421: keyCode = kbPgUp;   break;
        case 57422: keyCode = kbPgDn;   break;
        case 57423: keyCode = kbHome;   break;
        case 57424: keyCode = kbEnd;    break;
        case 57425: keyCode = kbIns;    break;
        case 57426: keyCode = kbDel;    break;
        default: if (isAlpha(value)) keyCode = value;
    }
    keyDown = keyWithXTermMods(keyCode, mods);
    if ( isAlpha(keyDown.keyCode) ||
         (keyDown.keyCode == 0 && ' ' <= value && !isPrivate(value)) )
    {
        uint32_t codepoint = keyDown.keyCode == 0 ? value : keyDown.keyCode;
        keyDown.textLength = utf32To8(codepoint, keyDown.text);
        keyDown.charScan.charCode =
            CpTranslator::printableFromUtf8({keyDown.text, keyDown.textLength});
    }
    return keyDown.keyCode != 0 || keyDown.textLength != 0;
}

static bool popPendingKittyEvent(InputState &state, TEvent &ev) noexcept
{
    KittyKeyboardState &kitty = state.kittyKeyboard;

    if (kitty.stateEventPending)
    {
        kitty.stateEventPending = false;
        ev.what = evKeyState;
        ev.keyState.controlKeyState = kitty.modifiers;
        return true;
    }

    while (kitty.pendingTextIndex < kitty.pendingTextCount)
    {
        uint32_t codepoint = kitty.pendingText[kitty.pendingTextIndex++];
        if (keyFromCodepoint(codepoint, kitty.pendingTextModifiers, ev.keyDown))
        {
            ev.what = evKeyDown;
            if (kitty.pendingTextIndex == kitty.pendingTextCount)
                kitty.pendingTextIndex = kitty.pendingTextCount = 0;
            return true;
        }
    }
    kitty.pendingTextIndex = kitty.pendingTextCount = 0;
    return false;
}

static bool keyFromLetter(uint letter, uint mod, KeyDownEvent &keyDown) noexcept
{
    ushort keyCode = 0;
    switch (letter)
    {
        case 'A': keyCode = kbUp; break;
        case 'B': keyCode = kbDown; break;
        case 'C': keyCode = kbRight; break;
        case 'D': keyCode = kbLeft; break;
        case 'E': keyCode = kbNoKey; break; // Numpad 5, "KP_Begin".
        case 'F': keyCode = kbEnd; break;
        case 'H': keyCode = kbHome; break;
        case 'P': keyCode = kbF1; break;
        case 'Q': keyCode = kbF2; break;
        case 'R': keyCode = kbF3; break;
        case 'S': keyCode = kbF4; break;
        case 'Z': keyCode = kbTab; break;
        // Keypad in XTerm (SS3).
        case 'j': keyCode = '*'; break;
        case 'k': keyCode = '+'; break;
        case 'm': keyCode = '-'; break;
        case 'M': keyCode = kbEnter; break;
        case 'n': keyCode = kbDel; break;
        case 'o': keyCode = '/'; break;
        case 'p': keyCode = kbIns; break;
        case 'q': keyCode = kbEnd; break;
        case 'r': keyCode = kbDown; break;
        case 's': keyCode = kbPgDn; break;
        case 't': keyCode = kbLeft; break;
        case 'u': keyCode = kbNoKey; break; // Numpad 5, "KP_Begin".
        case 'v': keyCode = kbRight; break;
        case 'w': keyCode = kbHome; break;
        case 'x': keyCode = kbUp; break;
        case 'y': keyCode = kbPgUp; break;
        default: return false;
    }
    keyDown = keyWithXTermMods(keyCode, mod);
    if (isAlpha(keyDown.keyCode))
    {
        keyDown.text[0] = keyDown.keyCode;
        keyDown.textLength = 1;
    }
    return true;
}

void TermIO::normalizeKey(KeyDownEvent &keyDown) noexcept
{
    TKey tKey(keyDown);
    ushort newMods = tKey.mods & (kbShift | kbLeftCtrl | kbLeftAlt | kbLeftSuper);
    ushort keyCodeMods = newMods & (kbShift | kbLeftCtrl | kbLeftAlt);
    if (keyCodeMods != 0)
        if (ushort keyCode = getModdedKeyCode(tKey.code, keyCodeMods))
        {
            keyDown.keyCode = keyCode;
            if (keyDown.charScan.charCode < ' ')
                keyDown.textLength = 0;
        }
    // TKey does not distinguish left/right modifiers, so preserve those
    // when available.
    ushort origMods = keyDown.controlKeyState;
    keyDown.controlKeyState =
        ((origMods | newMods) & ~(kbCtrlShift | kbAltShift | kbSuperShift))
      | ((origMods & kbCtrlShift ? origMods : newMods) & kbCtrlShift)
      | ((origMods & kbAltShift ? origMods : newMods) & kbAltShift)
      | ((origMods & kbSuperShift ? origMods : newMods) & kbSuperShift)
        ;
}

ParseResult TermIO::parseEvent(GetChBuf &buf, TEvent &ev, InputState &state) noexcept
{
    if (popPendingKittyEvent(state, ev))
        return Accepted;
    if (buf.get() == '\x1B')
        return parseEscapeSeq(buf, ev, state);
    return Rejected;
}

ParseResult TermIO::parseCSIKey(const CSIData &csi, TEvent &ev, InputState &state) noexcept
// https://invisible-island.net/xterm/xterm-function-keys.html
// https://invisible-island.net/xterm/ctlseqs/ctlseqs.html
{
    uint terminator = csi.terminator;
    uint length = csi.length;
    uint eventType = 1;

    if (csi.prefix != 0)
        return Rejected;
    if ( length == 3 &&
         csi.getSeparator(0) == ';' &&
         csi.getSeparator(1) == ':' )
    {
        eventType = csi.getValue(2);
        length = 2;
    }

    if (length == 1 && terminator == '~')
    {
        switch (csi.getValue(0))
        {
            case 1: ev.keyDown = {{kbHome}}; break;
            case 2: ev.keyDown = {{kbIns}}; break;
            case 3: ev.keyDown = {{kbDel}}; break;
            case 4: ev.keyDown = {{kbEnd}}; break;
            case 5: ev.keyDown = {{kbPgUp}}; break;
            case 6: ev.keyDown = {{kbPgDn}}; break;
            // Note that these numbers can be interpreted in different ways, i.e.
            // they could be interpreted as F1-F12 instead of F1-F10.
            // But this fallback is triggered by Putty, which uses F1-F10.
            case 11: ev.keyDown = {{kbF1}}; break;
            case 12: ev.keyDown = {{kbF2}}; break;
            case 13: ev.keyDown = {{kbF3}}; break;
            case 14: ev.keyDown = {{kbF4}}; break;
            case 15: ev.keyDown = {{kbF5}}; break;
            case 17: ev.keyDown = {{kbF6}}; break;
            case 18: ev.keyDown = {{kbF7}}; break;
            case 19: ev.keyDown = {{kbF8}}; break;
            case 20: ev.keyDown = {{kbF9}}; break;
            case 21: ev.keyDown = {{kbF10}}; break;
            case 23: ev.keyDown = {{kbShiftF1}, kbShift}; break;
            case 24: ev.keyDown = {{kbShiftF2}, kbShift}; break;
            case 25: ev.keyDown = {{kbShiftF3}, kbShift}; break;
            case 26: ev.keyDown = {{kbShiftF4}, kbShift}; break;
            case 28: ev.keyDown = {{kbShiftF5}, kbShift}; break;
            case 29: ev.keyDown = {{kbShiftF6}, kbShift}; break;
            case 31: ev.keyDown = {{kbShiftF7}, kbShift}; break;
            case 32: ev.keyDown = {{kbShiftF8}, kbShift}; break;
            case 33: ev.keyDown = {{kbShiftF9}, kbShift}; break;
            case 34: ev.keyDown = {{kbShiftF10}, kbShift}; break;
            case 200: state.bracketedPaste = true; return Ignored;
            case 201: state.bracketedPaste = false; return Ignored;
            default: return Rejected;
        }
    }
    else if (length == 1 && csi.getValue(0) == 1)
    {
        if (!keyFromLetter(terminator, XTermModDefault, ev.keyDown))
            return Rejected;
    }
    else if (length == 2 && csi.getSeparator(0) == ';')
    {
        uint mod = csi.getValue(1);
        if (csi.getValue(0) == 1)
        {
            if (!keyFromLetter(terminator, mod, ev.keyDown))
                return Rejected;
        }
        else if (terminator == '~')
        {
            ushort keyCode = 0;
            switch (csi.getValue(0))
            {
                case  2: keyCode = kbIns; break;
                case  3: keyCode = kbDel; break;
                case  5: keyCode = kbPgUp; break;
                case  6: keyCode = kbPgDn; break;
                case 11: keyCode = kbF1; break;
                case 12: keyCode = kbF2; break;
                case 13: keyCode = kbF3; break;
                case 14: keyCode = kbF4; break;
                case 15: keyCode = kbF5; break;
                case 17: keyCode = kbF6; break;
                case 18: keyCode = kbF7; break;
                case 19: keyCode = kbF8; break;
                case 20: keyCode = kbF9; break;
                case 21: keyCode = kbF10; break;
                case 23: keyCode = kbF11; break;
                case 24: keyCode = kbF12; break;
                case 29: keyCode = kbNoKey; break; // Menu key (XTerm).
                default: return Rejected;
            }
            ev.keyDown = keyWithXTermMods(keyCode, csi.getValue(1));
        }
        else
            return Rejected;
    }
    else if ( length == 3 &&
              csi.getValue(0) == 27 &&
              csi.getSeparator(0) == ';' &&
              csi.getSeparator(1) == ';' &&
              terminator == '~' )
    {
        // XTerm's "modifyOtherKeys" mode.
        uint key = csi.getValue(2);
        uint mod = csi.getValue(1);
        if (!keyFromCodepoint(key, mod, ev.keyDown))
            return Ignored;
    }
    else
        return Rejected;
    if (eventType == 3)
        return Ignored;
    if (eventType != 1 && eventType != 2)
        return Rejected;
    ev.what = evKeyDown;
    return Accepted;
}

ParseResult TermIO::parseSS3Key(GetChBuf &buf, TEvent &ev) noexcept
// https://invisible-island.net/xterm/xterm-function-keys.html
// Pre: "\x1BO" has just been read.
// Konsole, IntelliJ.
{
    uint mod;
    if (!buf.getNum(mod)) return Rejected;
    uint key = (uint) buf.last();
    if (!keyFromLetter(key, mod, ev.keyDown)) return Rejected;
    ev.what = evKeyDown;
    return Accepted;
}

static bool isTrackedKittyModifier(uint keyCode) noexcept
{
    switch (keyCode)
    {
        case 57441: // Left Shift.
        case 57442: // Left Control.
        case 57443: // Left Alt.
        case 57444: // Left Super.
        case 57447: // Right Shift.
        case 57448: // Right Control.
        case 57449: // Right Alt.
        case 57450: // Right Super.
            return true;
        default:
            return false;
    }
}

ParseResult TermIO::parseKittyKey(const CSIData &csi, TEvent &ev, InputState &state) noexcept
// https://sw.kovidgoyal.net/kitty/keyboard-protocol.html
// http://www.leonerd.org.uk/hacks/fixterms/
{
    if (csi.prefix != 0 || csi.length < 1 || csi.terminator != 'u')
        return Rejected;

    // Kitty events are structured like:
    //
    // unicode-key-code : shifted-key-code : base-layout-key ; modifiers : event-type ; text-as-codepoints u
    //
    // Only the 'unicode-key-code' is mandatory; the rest are optional.

    uint kittyKeyCode = 0;
    uint kittyShiftedKeyCode = 0;
    uint kittyBaseLayoutKey = 0;
    uint kittyModifiers = 1;
    uint kittyEventType = 1;
    uint kittyText = 0;

    uint i = 0;
    kittyKeyCode = csi.getValue(i++, 0);
    if (i < csi.length && csi.getSeparator(i - 1) == ':')
        kittyShiftedKeyCode = csi.getValue(i++, 0);
    if (i < csi.length && csi.getSeparator(i - 1) == ':')
        kittyBaseLayoutKey = csi.getValue(i++, 0);
    if (i < csi.length)
        kittyModifiers = csi.getValue(i++, 1);
    if (i < csi.length && csi.getSeparator(i - 1) == ':')
        kittyEventType = csi.getValue(i++, 1);
    if (i < csi.length)
        kittyText = csi.getValue(i++, 0);

    if (kittyModifiers < XTermModDefault)
        return Ignored;
    if (kittyEventType < 1 || kittyEventType > 3)
        return Ignored;

    KittyKeyboardState &kitty = state.kittyKeyboard;
    if (isTrackedKittyModifier(kittyKeyCode))
    {
        if (kitty.phase == kkpVerifying || kitty.phase == kkpActive)
        {
            ushort modifiers = modsFromXTerm(kittyModifiers);
            if (kitty.modifiers != modifiers)
            {
                kitty.modifiers = modifiers;
                if (kitty.phase == kkpActive)
                {
                    ev.what = evKeyState;
                    ev.keyState.controlKeyState = modifiers;
                    return Accepted;
                }
            }
        }
        return Ignored;
    }

    if (kittyEventType == 3)
        return Ignored;

    kitty.pendingTextIndex = kitty.pendingTextCount = 0;
    kitty.pendingTextModifiers = kittyModifiers;
    while (i < csi.length && kitty.pendingTextCount < KittyKeyboardState::maxPendingText)
        kitty.pendingText[kitty.pendingTextCount++] = csi.getValue(i++, 0);

    uint codepoint = kittyKeyCode;
    if (kittyText != 0)
        codepoint = kittyText;
    else if (kittyShiftedKeyCode != 0)
        codepoint = kittyShiftedKeyCode;

    if (!keyFromCodepoint(codepoint, kittyModifiers, ev.keyDown))
        return popPendingKittyEvent(state, ev) ? Accepted : Ignored;

    // When the unicode-key-code isn't an ASCII letter, but the base-layout-key
    // is, and at the same time the Ctrl or Alt modifiers are present,
    // initialize the event's keyCode as if it was Ctrl/Alt + A-Z, so that
    // standard keyboard shortcuts can still be triggered when using a non-ASCII
    // keyboard layout (e.g. Ctrl+Ф in the RU keyboard layout will match Ctrl+A).
    if ( !isAsciiLetter(kittyKeyCode) && isAsciiLetter(kittyBaseLayoutKey) &&
         (ev.keyDown.controlKeyState & (kbCtrlShift | kbAltShift)) != 0 )
    {
        // Kitty's 'base-layout-key' is always in lowercase,
        char upperBaseLayoutKey = kittyBaseLayoutKey - 'a' + 'A';
        ev.keyDown.keyCode = getModdedKeyCode(upperBaseLayoutKey, ev.keyDown.controlKeyState);
        // Note that 'ev.keyDown.text' still contains the original key text.
    }

    ev.what = evKeyDown;
    return Accepted;
}

} // namespace tvision
