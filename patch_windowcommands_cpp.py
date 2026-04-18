import re

with open("app/commands/MRWindowCommands.cpp", "r") as f:
    content = f.read()

target = "bool mrHandleAdjacentWindows() {"

replacement = """bool mrHandleCascadeWindows() {
	std::vector<TMREditWindow *> windows = allEditWindowsInZOrder();
	if (windows.empty())
		return true;

	std::reverse(windows.begin(), windows.end()); // Bottom to top

	TRect deskExtent = TProgram::deskTop->getExtent();

	for (std::size_t i = 0; i < windows.size(); ++i) {
		int offset = static_cast<int>(i) * 2;

		TRect bounds(offset, offset, deskExtent.b.x, deskExtent.b.y);
		windows[i]->changeBounds(bounds);
		windows[i]->drawView();
	}

	return true;
}

bool mrHandleTileWindows() {
	std::vector<TMREditWindow *> windows = allEditWindowsInZOrder();
	if (windows.empty() || windows.size() >= 10)
		return true;

	std::reverse(windows.begin(), windows.end());
	std::size_t count = windows.size();

	TRect deskExtent = TProgram::deskTop->getExtent();
	int w = deskExtent.b.x;
	int h = deskExtent.b.y;

	struct Layout {
		int rows;
		int cols[5]; // max 4 rows needed (e.g. for 8 windows)
	};

	Layout layout;
	if (count % 2 == 0) {
		layout.rows = (count == 2) ? 1 : 2;
		for (int i = 0; i < layout.rows; ++i)
			layout.cols[i] = static_cast<int>(count / layout.rows);
	} else {
		if (count == 1) {
			layout.rows = 1;
			layout.cols[0] = 1;
		} else if (count == 3) {
			layout.rows = 1;
			layout.cols[0] = 3;
		} else if (count == 5) {
			layout.rows = 2;
			layout.cols[0] = 3;
			layout.cols[1] = 2;
		} else if (count == 7) {
			layout.rows = 2;
			layout.cols[0] = 4;
			layout.cols[1] = 3;
		} else if (count == 9) {
			layout.rows = 3;
			layout.cols[0] = 3;
			layout.cols[1] = 3;
			layout.cols[2] = 3;
		}
	}

	int rowHeight = h / layout.rows;
	int windowIndex = 0;

	for (int r = 0; r < layout.rows; ++r) {
		int currentY = r * rowHeight;
		int nextY = (r == layout.rows - 1) ? h : (r + 1) * rowHeight;

		int colsInRow = layout.cols[r];
		int colWidth = w / colsInRow;

		for (int c = 0; c < colsInRow; ++c) {
			int currentX = c * colWidth;
			int nextX = (c == colsInRow - 1) ? w : (c + 1) * colWidth;

			TRect bounds(currentX, currentY, nextX, nextY);
			windows[windowIndex]->changeBounds(bounds);
			windows[windowIndex]->drawView();
			windowIndex++;
		}
	}

	return true;
}

bool mrHandleAdjacentWindows() {"""

content = content.replace(target, replacement)

with open("app/commands/MRWindowCommands.cpp", "w") as f:
    f.write(content)
