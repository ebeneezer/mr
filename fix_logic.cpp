#include <iostream>
// In tvision `event` is passed by reference.
// The problem is `TWindow::dragView` consumes the events, and clears the initial `evMouseDown` or `evCommand` event.
// At the end of `TView::dragView`, `event.what` might be zeroed or `event.mouse.where` might not accurately reflect the release coordinates.
