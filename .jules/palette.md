## 2026-04-16 - Keyboard Shortcuts in Turbo Vision Dialogs
**Learning:** In the Turbo Vision framework used by this project, keyboard shortcuts (mnemonics) for dialog buttons are defined by enclosing the shortcut letter in tildes (e.g., `O~K~`, `~C~ancel`). The framework automatically handles rendering the underscore/highlight and binds the shortcut key.
**Action:** Always check if dialog buttons (especially `TButton` instances) are missing tilde shortcuts, and add them for better accessibility.
