# AdaScript Calculator
#
# Type the numbers and operators, or move the cursor over a button with the
# arrow keys and press Enter. C removes the last digit, A clears everything.

mode screen

# The buttons, read left to right and top to bottom.
define labels: string[] = { "C", "A", "^", "/", "7", "8", "9", "*", "4", "5", "6", "-", "1", "2", "3", "+", "0", "00", ".", "=" }

define acc: float = 0
define entry: float = 0
define decimals: int32 = 0
define has_dot: bool = false
define pending: bool = false
define entering: bool = false
define op: string = ""
define show: string = "0"
define sel_r: int32 = 2
define sel_c: int32 = 1

# The frame, in the same double-line box characters the Tetris game draws:
# É » È ¼ corners, Í and º for the edges.
writecell 3 28 "É" color white
writecell 3 53 "»" color white
writecell 22 28 "È" color white
writecell 22 53 "¼" color white
writecell 4 29 "É" color white
writecell 4 52 "»" color white
writecell 6 29 "È" color white
writecell 6 52 "¼" color white

define i: int32 = 29
while i <= 52 do
  writecell 3 i "Í" color white
  writecell 22 i "Í" color white
  i = i + 1
endwhile

i = 30
while i <= 51 do
  writecell 4 i "Í" color white
  writecell 6 i "Í" color white
  i = i + 1
endwhile

i = 4
while i <= 21 do
  writecell i 28 "º" color white
  writecell i 53 "º" color white
  i = i + 1
endwhile

writecell 5 29 "º" color white
writecell 5 52 "º" color white
writecell 5 31 "0" color lightgreen

# The buttons: four columns and five rows, centred in the frame.
i = 0
while i < 20 do
  define r: int32 = i / 4
  define c: int32 = i - r * 4
  writecell (8 + r * 3) (32 + c * 6) labels[i] color lightgray
  i = i + 1
endwhile

writecell 24 28 "Arrows move, Enter presses, ^E quits" color darkgray

cursorconf visible true
cursorconf fixed false
setcursor 14 38

# An arrow key asks for a one-cell move; the handler cancels it and jumps the
# cursor to the middle of the next button instead.
on cursormove get row column do
  cancel

  define cr: int32 = 8 + sel_r * 3
  define cc: int32 = 32 + sel_c * 6

  if column != cc then
    if column > cc then
      if sel_c < 3 then
        sel_c = sel_c + 1
      endif
    else
      if sel_c > 0 then
        sel_c = sel_c - 1
      endif
    endif
  else
    if row > cr then
      if sel_r < 4 then
        sel_r = sel_r + 1
      endif
    else
      if sel_r > 0 then
        sel_r = sel_r - 1
      endif
    endif
  endif

  setcursor (8 + sel_r * 3) (32 + sel_c * 6)
endon

on keypress get key do
  define act: string = ""
  define slot: int32 = find labels key

  # A button's own key selects it and presses it.
  if slot >= 0 then
    sel_r = slot / 4
    sel_c = slot - sel_r * 4
    act = key
  endif

  # Enter and Space press the button the cursor is on.
  if key == "Enter" or key == "Space" then
    act = labels[sel_r * 4 + sel_c]
  endif

  if act == "" then
    # Not a button: leave the calculator as it is.
  else if act == "A" then
    acc = 0
    entry = 0
    decimals = 0
    has_dot = false
    pending = false
    entering = false
    op = ""
    show = "0"
  else if act == "C" then
    if entering then
      if has_dot and decimals > 0 then
        define scale: float = 1
        define j: int32 = 0
        while j < decimals do
          scale = scale * 10
          j = j + 1
        endwhile
        define cut: int32 = toint (entry * scale)
        cut = cut / 10
        decimals = decimals - 1
        define back: float = 1
        j = 0
        while j < decimals do
          back = back * 10
          j = j + 1
        endwhile
        entry = cut / back
      else if has_dot then
        has_dot = false
      else
        entry = toint entry / 10
      endif
      show = tostring entry
    endif
  else if isnumeric act then
    if not entering then
      entry = 0
      decimals = 0
      has_dot = false
      entering = true
    endif

    define digit: int32 = toint act
    define count: int32 = lengthof act
    define place: float = 1
    define j: int32 = 0
    while j < count do
      place = place * 10
      j = j + 1
    endwhile

    if has_dot then
      define grown: int32 = decimals + count
      define unit: float = 1
      j = 0
      while j < grown do
        unit = unit * 10
        j = j + 1
      endwhile
      entry = entry + digit / unit
      decimals = grown
    else
      entry = entry * place + digit
    endif

    if has_dot and decimals == 0 then
      show = tostring entry + "."
    else
      show = tostring entry
    endif
  else if act == "." then
    if not entering then
      entry = 0
      decimals = 0
      entering = true
    endif
    has_dot = true
    if decimals == 0 then
      show = tostring entry + "."
    else
      show = tostring entry
    endif
  else
    # An operator or "=": finish the calculation waiting in op first.
    define isop: bool = act != "="
    define bad: bool = false

    if pending then
      if op == "+" then
        acc = acc + entry
      else if op == "-" then
        acc = acc - entry
      else if op == "*" then
        acc = acc * entry
      else if op == "/" then
        if entry == 0 then
          bad = true
        else
          acc = acc / entry
        endif
      else if op == "^" then
        if entry < 0 then
          bad = true
        else
          define base: float = acc
          define power: int32 = toint entry
          define result_value: float = 1
          define j: int32 = 0
          while j < power do
            result_value = result_value * base
            j = j + 1
          endwhile
          acc = result_value
        endif
      endif
    else
      acc = entry
    endif

    if bad then
      acc = 0
      entry = 0
      decimals = 0
      has_dot = false
      pending = false
      entering = false
      op = ""
      show = "Error"
    else
      if isop then
        op = act
        pending = true
      else
        op = ""
        pending = false
      endif
      entering = false
      show = tostring acc
    endif
  endif

  # Show the value and put the cursor back on the selected button.
  erasecell 5 30 5 51
  define start: int32 = 51 - lengthof show
  if start < 31 then
    start = 31
  endif
  writecell 5 start show color lightgreen
  setcursor (8 + sel_r * 3) (32 + sel_c * 6)
endon

keepalive
