#ifndef ADAOS_EDITOR_H
#define ADAOS_EDITOR_H

// The full-screen text editor behind "write". It saves the shell's screen,
// then repaints the display from three parts: the title bar on row 0, the text
// on the rows between, and a status bar on the last row with the shortcuts and
// the paging markers "<" and ">" that mean something is hidden off screen.
//
// The text is an array of fixed-width lines, one screen row each. A line may be
// wider than the screen, so the view keeps a horizontal offset (first_col)
// beside the vertical one (first_line), and moving the cursor past either edge
// pages it one line or column, like a terminal. Editing is in place: insertion,
// deletion, line splitting and line joining, then a save that writes the whole
// buffer back to the file system.
//
// Files ending in ".as" are drawn with syntax colouring; every other file stays
// white. The colouring is decided one line at a time: a keyword goes light
// blue, a command name magenta, a data type red, a number outside a string
// orange, and a double-quoted string green from its opening quote to its
// closing one. A "#" and the rest of its line are a comment in light gray.
// Anything else keeps the default white.

/* Open a file in the editor, creating it if needed. */
void editor_open(const char* path);

#endif /* ADAOS_EDITOR_H */
