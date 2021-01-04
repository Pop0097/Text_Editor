#include <stdio.h>
#include <errno.h>
#include <ctype.h>
#include <unistd.h>
#include <termios.h>
#include <stdlib.h>

struct termios original_termios; 

void disable_raw_mode();
void enable_raw_mode();
void safe_exit(const char*);

int main(int argc, char ** argv) {
    enable_raw_mode(); // Before doing anything else, we must put terminal in correct mode

    while (1) {
        char input = '\0';
        
        // Reads one byte
        if (read(STDIN_FILENO, &input, 1) == -1 && errno != EAGAIN) {
            safe_exit("read"); // In case process fails
        }
        
        // Checks if the character input is a control character, which are nonprintable (ASCII 0-31)
        if (iscntrl(input)) { 
            printf("%d\r\n", input);
        } else {
            printf("%d ('%c')\r\n", input, input);
        }

        // If user wishes to quit program, program exits while loop
        if (input == 'q') {
            break;
        }
    }

    return 0;
}

/**
 * If an error occurs, this function is called and prints the error and then exits
 */ 
void safe_exit(const char * error) {
    perror(error);
    exit(1);
}

/**
 * When program exits, we must put terminal back in canonical mode. 
 */ 
void disable_raw_mode() {
    // Set original attributes
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_termios) == -1) {
        safe_exit("tcsetattr"); // In case process fails
    } 
}

/**
 * Terminals are normally in canonical mode (inputs are not processed until user pressed ENTER [reading inputs line-by-line]).
 * For the text editor, we want the terminal to be in raw mode (reading inputs byte-by-byte).
 *   
 */ 
void enable_raw_mode() {
    // Gets the current attributes
    if (tcgetattr(STDIN_FILENO, &original_termios) == -1) {
        safe_exit("tcgetattr"); // In case process fails
    } 
    atexit(disable_raw_mode); // This ensuers that disableRawMode() is called automatically when the program exits, no matter the manner with which the program exits

    struct termios rawMode = original_termios;

    // Disables (In the order presented): ECHO, canonical mode, Ctrl-C/Ctrl-Z/Ctrl-Y, Ctrl-V/Ctrl-O,
    rawMode.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN); // lflag stores local flags

    // Disables (In the order presented): Ctrl-S/Ctrl-Q. Last flag makes Ctrl-M output 13 instead of 10
    rawMode.c_iflag &= ~(IXON | BRKINT | INPCK | ISTRIP | ICRNL); // iflag stores input flags

    // Disables (in the order presented): output processing
    rawMode.c_oflag &= ~(OPOST); // oflag stores output flags

    rawMode.c_cflag |= (CS8);
    rawMode.c_cc[VMIN] = 0; // Minimum number of bytes needed before read() returns
    rawMode.c_cc[VTIME] = 1; // Maximum amount of time to wait before read() returns (measured in tenths bof a second)

    // Sets terminal attributes
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_termios) == -1) {
        safe_exit("tcsetattr"); // In case process fails 
    } 
}
