# AdaScript Demonstration Script

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
print "Array w length:", len(w) endl
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

# Variables deletion
delete a
delete b

print "Execution done." endl
