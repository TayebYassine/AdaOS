# AdaScript Demonstration Script

# Variables and types
define x: int8 = 14;
define name: string = "John";
define y: uint16;
define z: string = null;
define flag: bool = true;

print "x (int8):", x;
print "name (string):", name;
print "y (uint16 default):", y;
print "z (null string):", z;
print "flag (bool):", flag;

# Arrays
define w: uint32[] = { 10, 20, 30 };
define names: string[] = { "Alice", "Bob", "Charlie" };

print "Array w:", w;
print "Array w length:", len(w);
print "Element w[0]:", w[0];
print "Element w[1]:", w[1];

# Numeric loop
define i: uint8 = 0;
for i from 0 to 5 step 1 do
  print "Counter i is: " + i;
done;

# Array iteration loop
for j in names do:
  print "Hello, " + j + "!";
done;

# Conditions (if / else)
define score: int32 = 85;
if score >= 90 do
  print "Grade: A";
else if score >= 80 do
  print "Grade: B";
else
  print "Grade: C";
done;

# While loop
define count: int32 = 3;
while count > 0 do
  print "Countdown: " + count;
  count = count - 1;
  sleep 1
done;
print "Boom!";

# System commands
print "Working directory:";
pwd;

# Variables deletion
delete x;
delete y;
print "Variables x and y successfully deleted from RAM.";

print "Execution done."
