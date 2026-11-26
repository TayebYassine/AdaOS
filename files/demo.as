# AdaScript Demonstration Script CLI Mode

mode cli

# Variables and types
define a: int8 = 14
define b: string = "John"
define c: uint16
define d: string = null
define e: bool = true

# Arrays
define w: uint32[] = { 10, 20, 30 }
define names: string[] = { "Alice", "Bob", "Charlie" }

print "Array w:", w endl
print "Array w length:", sizeof w endl
print "First element w[0]:", w[0] endl
print "Second element w[1]:", w[1] endl

# Numeric loop
define i: uint8 = 0
for i from 0 to 3 step 1 do
  print "Counter i is: " + i endl
endfor

# Array iteration loop
for j in names do:
  print "Hello, " + j + "!" endl
endfor

# Conditions (if / else)
define score: int32 = 85
if score >= 90 do
  print "Grade: A" endl
else if score >= 80 do
  print "Grade: B" endl
else
  print "Grade: C" endl
endif

# While loop
define count: int32 = 3
print "Countdown: "
while count > 0 do
  print count
  count = count - 1
  if count > 0 do
    print ", "
  endif
  sleep 1
endwhile

print endl

# System commands
print "Working directory:", pwd endl

# User input
print "How old are you (0 to 127)? "
input a typeof uint8
print "What is your name (1 to 10 characters)? "
input b typeof string range 1 10
print "Pick a port (0 to 65535): "
input c typeof uint16
print "Married (0 = no, 1 = yes)? "
input e typeof bool

print "Age:", a endl
print "Name:", b endl
print "Port:", c endl
print "Married:", e endl

# Built-in functions, called by name without parentheses
define word: string = "AdaOS"
print "lengthof word:", lengthof word endl
print "toupper word:", toupper word endl
print "startswith:", startswith word "Ada" endl
print "find OS:", find word "OS" endl
print "tostring 123:", tostring 123 endl
print "toint 42:", toint "42" endl

# Variables deletion
delete a
delete b

print "Execution done." endl
