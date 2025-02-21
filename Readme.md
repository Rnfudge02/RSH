# To Build and Run
1. Ensure build-essential package is installed (Ubuntu 22.04)
2. Use make all, which will compile the package with debugging flags and start the program to gdb

# To Use
1. Run the program
2. Access any programs in directories contained in the system PATH variable
3. Use CTRL+C or exit to quit the program

# Features
1. Proper CTRL+C and CTRL+V implementation for subprocesses and shell
2. Foreground and background shell processing using subgroups
3. Pipes using the '|' operator
4. 'history' command

## Extra Functionality
4. Ability to view suspended processes using the 'jobs' command
5. 'exit' command
6. 'clear' command

# Known Issues
1. Because the terminal is operating in raw mode, the terminal recieves only '\n' from
most linux programs, however, the terminal requires carriage return '\r' and then newline '\n'

# Credit
Program developed by Robert Fudge, 2025

The code to implement raw mode is based on the code from [this link]()