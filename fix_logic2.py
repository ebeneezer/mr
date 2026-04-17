# In TView::dragView, the do-while loop ends when mouseEvent returns false.
# mouseEvent(event, evMouseMove) returns False when event.what == evMouseUp
# But inside the loop, event.mouse.where += p; modifies event.mouse.where.
# Wait, event.mouse.where += p! `p` is the offset between the origin and the initial mouse click.
# So event.mouse.where becomes the `origin` of the window, not the actual mouse coordinates!
# Let's verify this!
# If event.mouse.where is modified, it's useless for edge detection based on the MOUSE.
# I need to get the current global mouse position, or check if the WINDOW itself touches the edge!
# "Werden diese mit der Maus an die Seitenränder des Desktops gezogen wird das Textfenster..."
# If the user drags the window with the mouse to the edge of the desktop...
# It's better to check if the WINDOW origin (or bounds) hits the desktop edge.
# If `window->origin.x <= 0` (left edge of desktop).
