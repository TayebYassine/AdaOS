<p align="center">
  <img src="./assets//ada.png" alt="AdaOS logo" width="128" />
</p>

<h1 align="center">AdaOS</h1>

**AdaOS** is a tiny command-line operating system for 32-bit x86, written in C and Assembly. It boots through GRUB and drops you into a DOS-style shell.

This operating system has most of the important features, such as file management for reading and writing files, and a custom executable script called **AdaScript**. You can play games too!

> [!CAUTION]
> AdaOS is still experimental and may have bugs. Please report if you encounter any.
> Run it in a virtual machine such as [VirtualBox](https://www.virtualbox.org/) or [QEMU](https://www.qemu.org/) rather than on real hardware.

## Features

- **AdaScript**: Run script (.as) files with variables, numeric and array for loops, while loops, conditionals, arrays, and built-in OS commands.
- **DOS-style shell**: An 80x25 text shell that parses command lines, runs the built-in commands and documents them through `help`.
- **File system on disk**: A hierarchical tree of files and directories stored on the first IDE disk through the ATA driver, with `ls`, `cd`, `cat`, `mkdir`, `cp`, `mv`, `rm` and `find`. Without a disk the tree lives in RAM for the session.
- **Full-screen text editor**: `write <file>` opens an editor with a status line and cursor movement.
- **Games**: `snake` runs a playable snake game, and `tetris` runs a playable tetris game on the text-mode screen.
- **Memory management**: Physical page allocator, paging, and a kernel heap, summarised by `mem`.
- **Multitasking**: A preemptive scheduler with kernel tasks, listed by `tasks`.
- **Date and time**: The CMOS real-time clock, settable with `setdate` and `settime`, plus `time`, `cal` and `uptime`.
- **Text-mode UI**: 80x25 VGA output, CP437 box-drawing glyphs, PS/2 keyboard input and serial logging.

## View

<details>
<summary><b>First boot and setup on VirtualBox</b></summary>
  On boot, AdaOS will detect if the disk has a file system or not. If it doesn't, it will show the setup menu, as shown below. It will ask basic yes/no questions for configuring the operating system.

  Once the setup is done, it will reboot into the AdaOS shell.

  <img src="./assets/first-boot.gif" />
</details>

<details>
<summary><b>Using commands in the shell</b></summary>
  AdaOS is a CLI operating system, not a GUI operating system. You have to type every command with your keyboard, and it currently reads PS/2 keyboard keys only.

  <img src="./assets/using-commands.gif" />
</details>

<details>
<summary><b>Snake and Tetris!</b></summary>
  AdaOS provides you with two games, Snake and Tetris!

  - Snake: Grow your tail by eating apples, which are red-colored character cells on your screen.
  - Tetris: Tetrominoes are dropping down; try to make a line or more to be filled to get cleared and earn a score.

  <img src="./assets/games.gif" />
</details>

<details>
<summary><b>The AdaScript scripting language</b></summary>
  In shell, you can only run one command at once. A new scripting language was introduced called <b>AdaScript</b>, which executes an instruction each by line, with new special-specific commands and functions such as `define` to register a variable and its value in RAM, specific bit-sized datatypes such as `int8`, `uint16`, `int32`, etc.

  If you've accepted sample files on setup, run the command `ls` or `tree` to see a file named demo.as. This file contains every instruction added into the script language. Use the command `write demo.as` to edit it and use `run demo.as` to execute it.

  A script can also draw on the whole screen, and every script declares a mode on its first line. The complete language reference; modes, types, statements, loops and every built-in function, is under [AdaScript](#adascript).

  <img src="./assets/adascript.gif" />
</details>


## AdaScript

AdaScript is the scripting language that `run` executes. A script is a plain text file ending in `.as` with one statement per line: `write <file>` opens it in the editor and `run <file>` executes it. Setup creates three samples: `demo.as`, a `cli` script that uses most of the language, `screen.as`, which draws on the whole screen, and `calculator.as`, a working calculator built from listeners. This section is the reference for all three.

### Modes

| Mode | First line | Extra statements it allows |
|------|------------|----------------------------|
| Command line | `mode cli` | `print`, `input` |
| Screen | `mode screen` | `print`, `writecell`, `erasecell`, `clearscreen`, `cursorconf`, `setcursor`, `keepalive` |

Every script must declare its mode on the first line, before anything else. A file without a mode line, or with a name that is not `screen` or `cli`, stops with an error and runs nothing.

The mode also decides which statements may appear anywhere in the file. A `cli` script cannot use `writecell`, `erasecell`, `clearscreen`, `cursorconf`, `setcursor` or `keepalive`, or switch to `mode screen`, and a `screen` script cannot use `input`. `print` works in both modes: in `cli` mode it writes to the shell, and in `screen` mode it writes at the screen cursor. The whole file is checked before its first statement runs, so a statement from the other mode is reported by line before the script can change the screen.

`mode screen` saves the shell's screen, clears it, and gives the script all 80x25 cells. The saved screen comes back by itself when the script ends. A screen script may run `mode cli` to hand the screen back early and keep going.

Ctrl+E stops a running script at once, in either mode, even one that never reaches `keepalive`. A stopped `cli` script prints ` ^E (Abort)` and gives the prompt back on a new line. A script that registered an `on exit` listener runs it first, as described under [Listeners](#listeners).

### Lines, comments and limits

- One statement per line. Leading spaces or tabs are ignored, and a closing `;` is optional.
- A comment starts with `#`, `//` or `--` at the beginning of a word and runs to the end of the line. Inside a quoted string they are ordinary text.
- Variable names and text compare case-sensitively. The colour names, the words `tobool` reads, and the mode name are the exceptions.

| Limit | Value |
|-------|-------|
| Characters in a variable name | 31 |
| Characters in a string | 255 |
| Items in one array | 256 |
| Nesting of one expression | 6 levels |
| Nesting of blocks (`if`, `for`, `while`, `on`) | 8 levels |
| Nested `run` calls | 8 |

### Variables and types

`define` declares a variable and its type; the starting value is optional.

```adascript
define age: uint8 = 30
define name: string = "Ada"
define flag: bool
define scores: int16[] = { 10, 20, 30 }
```

| Type | Range or meaning |
|------|------------------|
| `int8` | -128 to 127 |
| `uint8` | 0 to 255 |
| `int16` | -32768 to 32767 |
| `uint16` | 0 to 65535 |
| `int32`, `int` | -2147483648 to 2147483647 |
| `uint32`, `uint` | 0 to 4294967295 |
| `float` | A decimal number, printed with up to four decimals |
| `string` | Text, up to 255 characters |
| `bool` | `true` or `false` |
| `type[]` | An array of any of the types above, as in `string[]` |

- A `define` with no `= value` starts as null (or an empty array).
- Numbers are decimal, or hexadecimal with `0x`, as in `0xFF`. A decimal point makes the number a float, as in `3.5` or `0.25`.
- `null` is the empty value: `define city: string = null`.
- `name = expression` stores into an existing variable, and creates one with the value's type when the name does not exist yet.
- `delete name` removes a variable and frees it.
- Array items are read and written with `arr[index]`, counting from 0: `scores[0]`, `scores[1] = 99`. An index outside the array stops the script.
- Assignment converts the value to the variable's type; a number too large for the type wraps to fit its width.
- `print` shows an array as `[ 10, 20, 30 ]`, a bool as `true` or `false`, null as `null`, and a string without its quotes.

### Operators

From the tightest binding to the loosest:

| Level | Operators | Meaning |
|-------|-----------|---------|
| 1 | `()` `[]` | Grouping and array indexing |
| 2 | `-` `!` `not` | Negation and logical not |
| 3 | `*` `/` `%` | Multiply, divide, remainder |
| 4 | `+` `-` | Add, subtract; `+` also joins text |
| 5 | `<` `<=` `>` `>=` | Ordering |
| 6 | `==` `!=` | Equality |
| 7 | `&&` `and` | Logical and |
| 8 | `\|\|` `or` | Logical or |

- `+` joins instead of adding whenever either side is text, so `"Score: " + 10` is `Score: 10`.
- `==` and the ordering operators compare text case-sensitively, bools as bools, and everything else as numbers.
- `null == null` is true.
- `&&` and `and`, `||` and `or`, are the same operator written two ways, and both give a bool.
- A value counts as false when it is null, zero, an empty string, or an empty array.
- When either side of `+`, `-`, `*` or `/` is a float, the whole operation is done in floats and gives a float, so `7 / 2.0` is `3.5` while `7 / 2` is `3`. The remainder `%` needs whole numbers.
- Dividing or taking the remainder by zero stops the script with an error.

### Printing and input

`print` writes values on one line, separated by a single space, followed by a newline only when `endl` is given. Separate the values with commas:

```adascript
print "Age:", age, "years" endl
print "No newline yet..."
print endl
```

In `cli` mode `print` writes to the shell. In `screen` mode it writes at the screen cursor instead of the shell: the point `writecell` left it on, starting at the top-left of the cleared screen. `input` is `cli` only.

`input` reads one line from the keyboard into a variable, and keeps asking until the answer fits:

```adascript
input "Your age? " age typeof uint8 range 18 120
input name typeof string range 1 10
```

- The quoted prompt is optional. Without `typeof`, an existing variable keeps its type and a new one is a string.
- `typeof` takes any scalar type, not an array.
- `range` takes a minimum, or a minimum and a maximum, and narrows the type's own bounds. For a string the range is the allowed length.

### Conditions

```adascript
if score >= 90 do
  print "A" endl
else if score >= 80 do
  print "B" endl
elif score >= 70 do
  print "C" endl
else
  print "F" endl
endif
```

The condition ends with `do` or `then`. Branches are `else if` or `elif`, and one `else`. The block closes with `endif`, `fi` or `done`.

### Loops

Counting loop, from a start to a limit, with an optional step that may be negative:

```adascript
for i from 0 to 9 step 1 do
  print i endl
endfor
```

`from` may be left out to continue from the counter's current value. The step defaults to 1, and a step of 0 is an error. A counter that would wrap past its type's range stops the script.

Loop over every item of an array, in order:

```adascript
for name in names do
  print "Hello, " + name + "!" endl
endfor
```

The loop variable is created with the array's item type when it does not exist, and holds a copy of each item. A `for` loop closes with `endfor` or `done`.

Condition loop, which re-reads its condition before every pass:

```adascript
while count > 0 do
  count = count - 1
endwhile
```

A `while` loop closes with `endwhile` or `done`, so `done` closes any block.

Inside a loop, `break` leaves it and `continue` starts the next pass. Anywhere in a script, `exit` or `return` ends the script immediately.

### Built-in functions

Functions are called by name, without parentheses: the name first, then its arguments, separated by spaces. Each argument binds tighter than any operator, so `lengthof name + 1` is the length plus one.

| Function | Arguments | Result |
|----------|-----------|--------|
| `lengthof text` | 1 | Number of characters in a string |
| `sizeof array` | 1 | Number of items in an array |
| `find subject needle` | 2 | Position of the first match, counting from 0, or -1 |
| `startswith text prefix` | 2 | `true` when the text begins with the prefix |
| `endswith text suffix` | 2 | `true` when the text ends with the suffix |
| `includes text needle` | 2 | `true` when the text contains the needle |
| `toupper text` | 1 | A new string in upper case |
| `tolower text` | 1 | A new string in lower case |
| `tostring value` | 1 | The text form of any value |
| `toint value` | 1 | The value as a whole number |
| `tobool value` | 1 | The value as a bool |
| `isnumeric text` | 1 | `true` when the text is one or more digits |
| `isalpha text` | 1 | `true` when the text is not empty and holds no digit |
| `isalphanumeric text` | 1 | `true` when the text holds at least one digit and at least one other character |
| `replace text from to` | 3 | A new string with every `from` replaced by `to` |

- `lengthof` refuses an array and `sizeof` refuses a string, each pointing at the other.
- `find` on a string searches for text and counts characters, and an empty needle is found at position 0. On an array it compares items, and the needle must be the same kind as the items: numbers of any width may be searched for with a number, text with text, a bool with a bool. Searching a `string[]` for `4` stops the script, and so does searching an `int16[]` for `"4"`.
- `startswith`, `endswith` and `includes` compare case-sensitively.
- `toint` reads a whole decimal number, with an optional leading `+` or `-`; anything else, as in `toint "ab123"`, stops the script. A number passes through unchanged, a bool becomes 0 or 1, and null is an error.
- `tobool` accepts `true` and `false` in any case, a whole number (zero is false), a bool, or null (false). Any other text stops the script.
- `replace` replaces every occurrence, and stops the script rather than cutting the result short when it would exceed 255 characters.
- A function only runs when no variable has its name: `define find: int32 = 3` hides `find` for the rest of the script.
- Functions return strings that are freed with the expression, so they can be nested and joined like any value: `print toupper replace word "OS" "OS/2" endl`.

```adascript
define word: string = "AdaOS"

print lengthof word               # 5
print toupper word                # ADAOS
print find word "OS"              # 2
print replace word "Ada" "My "    # My OS
print toint "42" + 1              # 43
print isnumeric "1234"            # true

if startswith word "Ada" and includes word "OS" do
  print "it is AdaOS" endl
endif
```

### Screen mode

`writecell row column text [color name]` writes at one cell. Rows count 1 to 25 from the top and columns 1 to 80 from the left. The text is the rest of the line as an expression, so it may contain spaces and operators, and the optional colour comes last; without a colour the text is white. The cursor is left just past the text it drew, which is where `print` continues.

```adascript
writecell 1 1 "AdaOS" color yellow
writecell 3 1 "Hello, " + name color lightcyan
```

`erasecell row column` clears one cell, and `erasecell row1 col1 row2 col2` clears the rectangle between the two corners, both included. `clearscreen` takes no arguments and clears every cell.

`cursorconf visible true` shows the blinking cursor and `cursorconf visible false` hides it; a screen script starts with it hidden. `cursorconf fixed true` leaves the cursor where it is when an arrow key is pressed, and `cursorconf fixed false` lets the arrow keys move it one cell, which is the default. Both take a boolean.

```adascript
cursorconf visible true
cursorconf fixed false
```

`setcursor row column` puts the hardware cursor on one cell, counting rows and columns like `writecell`; it moves the cursor and draws nothing. A `cursormove` handler can use it to jump the cursor over the cells between the items it moves between, which is how the keypad in `calculator.as` steps from one button to the next instead of one character at a time.

`keepalive` takes no arguments and keeps the script alive until Ctrl+E is pressed. In screen mode it delivers `keypress` and `cursormove` events and lets the arrow keys move the cursor while it waits.

The colour names are `black`, `blue`, `green`, `cyan`, `red`, `magenta`, `brown`, `lightgray` (or `lightgrey`), `darkgray` (or `darkgrey`), `lightblue`, `lightgreen`, `lightcyan`, `lightred`, `lightmagenta`, `yellow` and `white`.

```adascript
mode screen

writecell 1 1 "AdaOS screen mode" color yellow
writecell 3 1 "Press Ctrl+E to stop" color lightgray

define row: int32 = 5
while row <= 10 do
  writecell row 1 "=" color blue
  row = row + 1
endwhile

keepalive
```

### Listeners

A listener is a block that runs when something happens, instead of where the line stands. The keyword is `on`, followed by the event, an optional `get` listing the variables the event hands over, and `do`; `endon` closes the block (`done` and `enddo` work too).

```adascript
on keypress get key do
  writecell 3 1 "You pressed " + key color green
endon
```

The `on` line records the handler and the script continues with the line after `endon`. The body runs later, only when its event happens. There is one handler per event: a second `on` for the same event replaces the first.

| Event | Mode | Variables | Runs when |
|-------|------|-----------|-----------|
| `keypress` | `screen` | `get key` | a key on the keyboard is pressed |
| `cursormove` | `screen` | `get row column` | an arrow key asks to move the cursor, before it moves |
| `exit` | `screen` and `cli` | none | Ctrl+E is pressed, once, before the script stops |

`on keypress` and `on cursormove` stop a `cli` script with a mode error. A script with either of them stays alive after its last statement, waiting for events, as if it ended with `keepalive`; Ctrl+E still stops it. `on exit` alone does not keep a script alive.

**`keypress`** hands its variable the key as text:

| Key | Value |
|-----|-------|
| A letter | `"A"` to `"Z"`, always uppercase |
| A digit | `"0"` to `"9"` |
| A symbol | the character as typed, such as `"&"`, `"?"` or `"+"` |
| Space, Backspace, Enter | `"Space"`, `"Backspace"`, `"Enter"` |
| Escape | `"Escape"` |
| Arrow keys | `"Up"`, `"Down"`, `"Left"`, `"Right"` |
| Ctrl with a letter | `"Ctrl+S"`, `"Ctrl+R"`, ... |

Ctrl+E is not a `keypress`: it is the `exit` event.

**`cursormove`** runs when an arrow key is pressed in screen mode, before the cursor is moved. It hands the cell the cursor would move to as a row and a column, both counting from 1 like `writecell`. Call `cancel` inside the handler to keep the cursor where it is; without it the cursor moves to that cell when the handler ends. Drawing done by the script does not trigger the event, and drawing inside the handler does not trigger it again.

**`cancel`** stops the move the current event describes. It may only be written inside a listener body; anywhere else it is an error. Only `cursormove` uses it so far. A handler can refuse the cells it does not want, which keeps the cursor inside an area:

```adascript
mode screen
cursorconf visible true

writecell 5 10 "Start" color lightgray

on cursormove get row column do
  if row < 5 or row > 10 or column < 10 or column > 40 do
    cancel
  endif
endon

keepalive
```

**`exit`** runs after Ctrl+E is pressed and before the script stops, which is the place to free variables or draw a last message. The press is then consumed: the script stops cleanly instead of in the middle of a statement. In `cli` mode the usual ` ^E (Abort)` note still follows.

```adascript
mode screen

define hits: int32 = 0

on keypress get key do
  if key == "A" do
    hits = hits + 1
    writecell 3 1 "A pressed " + tostring hits + " times" color green
  endif
endon

on cursormove get row column do
  writecell 5 1 "Cursor: row " + tostring row + ", column " + tostring column color lightgray
endon

on exit do
  delete hits
endon

writecell 1 1 "Press A, then Ctrl+E to stop." color yellow
keepalive
```

The variables from `get` are local to the handler: a name the script already had gets its old value back afterwards, and a name the event created is deleted. They are not visible to the rest of the script.

### Shell commands in a script

Any shell command may be a statement, so a script can call `ls`, `cat`, `mkdir`, `cp`, `mv`, `rm`, `sleep`, `clear`, `run` and the rest.

- `$name` outside quotes is replaced with the variable's value, so `cd $folder` changes to the directory held in `folder`. Text inside quotes is passed as written, which is how a path with spaces in it works.
- Commands that print a result can be used as a value in an expression: `help`, `uptime`, `mem`, `tasks`, `ls`, `pwd`, `cat`, `find`, `tree`, `disk`, `time`, `cal`, `ver` and `run`. `print "Working in:", pwd endl` prints the current directory.
- `cd`, `cat`, `ls`, `tree`, `mkdir`, `rm`, `find` and `run` take their argument as an expression when it starts with one of those commands, as in `cd pwd + "/programs"`.
- `snake`, `tetris`, `write`, `reboot`, `shutdown`, `settime` and `setdate` cannot be run from a script and stop it with an error.
- A bare `find` line runs the shell command that searches files; `find` inside an expression is the built-in function that searches an array or a string.

### Errors

A script stops at its first error. The line and column are printed with the message, as in `Error: variable 'total' not found.`, and the script's variables are freed. In screen mode the shell's screen is restored first, then the error is shown on it, so it is not lost with the canvas.

### A complete script

```adascript
# A small cli script

mode cli

define name: string = "Ada"
define scores: int16[] = { 12, 7, 25 }

print "Hello, " + name + "!" endl
print "Scores:", scores, "count:", sizeof scores endl

define total: int32 = 0
for s in scores do
  total = total + s
endfor
print "Total:", total endl

define i: int32 = 0
while i < sizeof scores do
  if scores[i] > 10 do
    print "Over ten:", scores[i] endl
  endif
  i = i + 1
endwhile

if startswith name "A" and lengthof name > 2 do
  print toupper name endl
endif
```

## Requirements

List of minimum hardware requirements for AdaOS to work properly.

| Component  | Minimum                           | Notes                                                                                                                                  |
|------------|-----------------------------------|----------------------------------------------------------------------------------------------------------------------------------------|
| CPU        | 32-bit x86 (i386 or newer)        | Runs in 32-bit protected mode; x86-64 CPUs work in their legacy 32-bit mode.                                                            |
| Firmware   | Legacy BIOS (or CSM)              | **No UEFI support!** An EFI-only machine will not boot the ISO.                                                                        |
| RAM        | 32 MB                             | |
| Boot media | CD/DVD drive, or USB stick        | `adaos.iso` is a hybrid image; writing it to a USB stick with `dd` also boots on BIOS machines.                                         |
| Display    | VGA-compatible adapter, text mode | Text mode only (80x25), no graphics mode and no graphics driver.                                                                        |
| Keyboard   | PS/2 keyboard                     | USB keyboards only work where the firmware or VM emulates PS/2.                                                |
| Storage    | IDE disk (optional)               | The first ATA disk on the primary IDE bus holds the file system. Without a disk, the tree lives in RAM for the session. |

AdaOS has been tested with QEMU (`qemu-system-i386`) and VirtualBox 7.2. In VirtualBox use the `Other` (32-bit) type, BIOS firmware (not EFI), an IDE controller and 32 MB of RAM or more.

## Commands

| Command           | Description                                     |
|-------------------|-------------------------------------------------|
| `cal`             | Show this month's calendar                      |
| `cat <path>`      | Print a file                                    |
| `cd <path>`       | Change the current directory                    |
| `clear`           | Clear the screen                                |
| `cp <src> <dst>`  | Copy a file or a directory and its contents     |
| `disk`            | Show the disk and its file system               |
| `find <name>`     | Find files whose name contains <name>           |
| `help`            | List available commands                         |
| `ls [path]`       | List a directory                                |
| `mem`             | Show RAM, paging, the heap and the kernel size  |
| `mkdir <path>`    | Create a directory                              |
| `mv <src> <dst>`  | Move or rename a file or a directory            |
| `print <text>`    | Print text to the screen                        |
| `pwd`             | Show the current directory                      |
| `reboot`          | Restart the machine                             |
| `rm <path>`       | Remove a file or a directory and its contents   |
| `run <path>`      | Run an AdaScript file                           |
| `setdate`         | Set the date                                    |
| `settime`         | Set the time of day                              |
| `shutdown`        | Power off the machine                           |
| `sleep <sec>`     | Pause for a number of seconds                   |
| `snake`           | Play the snake game                             |
| `tasks`           | Show the running tasks                          |
| `tetris`          | Play the tetris game                            |
| `time`            | Show the date and the time                      |
| `tree [path]`     | Show a directory as a tree                      |
| `uptime`          | Show how long AdaOS has been running            |
| `ver`             | Show the AdaOS version                          |
| `write <file>`    | Edit a file in the full-screen editor           |

## Building

Requirements: `gcc` (with 32-bit support), `nasm`, `ld`, `grub-mkrescue`, `xorriso`, and QEMU or VirtualBox.

```bash
./build-iso.sh
qemu-system-i386 -cdrom adaos.iso
```
