# AdaScript Demonstration Script Screen Mode

mode screen

# A blue border around the screen.
define col: int32 = 1
while col <= 80 do
  writecell 1 col "=" color blue
  writecell 25 col "=" color blue
  col = col + 1
endwhile

define row: int32 = 2
while row <= 24 do
  writecell row 1 "|" color blue
  writecell row 80 "|" color blue
  row = row + 1
endwhile

# Text at a cell. "Hello World" starts at column 5, so H is at 5, e at 6, and
# so on; anything past column 80 is clipped at the edge.
writecell 3 4 "AdaOS screen mode" color yellow
writecell 5 4 "Hello World" color green
writecell 6 4 "Cells are 1-based: this is row 6, column 4." color lightgray
writecell 7 4 "No colour was given here, so the text is white."

# A row of words, each in its own colour.
writecell 9 4  "red" color red
writecell 9 9  "green" color green
writecell 9 16 "cyan" color cyan
writecell 9 22 "yellow" color yellow

# Fill a block, then clear the whole rectangle with one statement.
define x: int32 = 0
while x < 20 do
  writecell 12 (5 + x) "#" color magenta
  writecell 13 (5 + x) "#" color magenta
  x = x + 1
endwhile

sleep 2
erasecell 12 5 13 24
writecell 12 5 "(the block was cleared)" color lightcyan

# One point clears one cell.
writecell 15 5 "X" color red
sleep 1
erasecell 15 5
writecell 15 5 "erasecell 15 5 removed the X" color lightgray

# The screen stays up for a moment, then the shell's screen comes back on its
# own; there is no "mode cli" needed at the end.
sleep 3
