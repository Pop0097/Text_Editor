#include <stdio.h>
#include <errno.h>
#include <ctype.h>
#include <unistd.h>
#include <termios.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <string.h>

// Gets ASCII value of Ctrl-k by setting bits 5-7 as 0
#define CTRL_KEY(letter) ((letter) & 0x1f) 
// Default constructor for appendBuffer structure
#define APPENDBUFFER_INIT {NULL, 0}
#define PROGRAM_VERSION "0.0.1B"

// Stores the configuration of the terminal and editor
struct editorConfiguration {
    int cursorX, cursorY, windowRows, windowCols;
    struct termios original_termios; 
};

// This allows us to create our own dynamic string 
struct appendBuffer {
    char *buf;
    int length;
};

struct editorConfiguration eConfig;

enum customKeyValues {
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN
};

void init();
void safe_exit(const char*);
void disable_raw_mode();
void enable_raw_mode();
int get_window_size(int*, int*);
int get_cursor_position(int*, int*);
void refresh_screen();
void draw_rows(struct appendBuffer*);
int read_key();
void move_cursor(int);
void process_key_press();
void append_to_append_buffer(struct appendBuffer*, const char*, int);
void free_append_buffer(struct appendBuffer*);

int main() {
    enable_raw_mode(); // Before doing anything else, we must put terminal in correct mode
    init();

    while (1) {
        refresh_screen();
        process_key_press();
    }

    return 0;
}

/**
 * Initializes the editorConfiguration object
 */
void init() {
    eConfig.cursorX = 0; // Horizontal
    eConfig.cursorY = 0; // Vertical

    if (get_window_size(&eConfig.windowRows, &eConfig.windowCols) == -1) {
        safe_exit("get_window_size");
    }
} 

/**
 * If an error occurs, this function is called and prints the error and then exits
 */ 
void safe_exit(const char * error) {
    // Clears screen
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    perror(error); // Prints error
    exit(1); 
}

/**
 * When program exits, we must put terminal back in canonical mode. 
 */ 
void disable_raw_mode() {
    // Set original attributes
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &eConfig.original_termios) == -1) {
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
    if (tcgetattr(STDIN_FILENO, &eConfig.original_termios) == -1) {
        safe_exit("tcgetattr"); // In case process fails
    } 
    atexit(disable_raw_mode); // This ensuers that disableRawMode() is called automatically when the program exits, no matter the manner with which the program exits

    struct termios rawMode = eConfig.original_termios;

    // Disables (In the order presented): ECHO, canonical mode, Ctrl-C/Ctrl-Z/Ctrl-Y, Ctrl-V/Ctrl-O,
    rawMode.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG); // lflag stores local flags

    // Disables (In the order presented): Ctrl-S/Ctrl-Q. Last flag makes Ctrl-M output 13 instead of 10
    rawMode.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON); // iflag stores input flags

    // Disables (in the order presented): output processing
    rawMode.c_oflag &= ~(OPOST); // oflag stores output flags

    rawMode.c_cflag |= (CS8);
    rawMode.c_cc[VMIN] = 0; // Minimum number of bytes needed before read() returns
    rawMode.c_cc[VTIME] = 1; // Maximum amount of time to wait before read() returns (measured in tenths bof a second)

    // Sets terminal attributes
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &rawMode) == -1) {
        safe_exit("tcsetattr"); // In case process fails 
    } 
}

/**
 *  Returns the size of the window in terms of rows and columns
 */ 
int get_window_size(int *rows, int *cols) {
    struct winsize windowSize;

    // If getting window properties is successful, set values. 
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &windowSize) == -1 || windowSize.ws_col == 0) {
        // Sometimes ioctl may fail because of compatability issues. In this case, we will first try to manually determine size
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) { // moves cursor to bottom right
            return -1;
        }
        return get_cursor_position(rows, cols); 
    } else {
        *cols = windowSize.ws_col;
        *rows = windowSize.ws_row;
        return 0;
    }
}

/**
 * 
 */
int get_cursor_position(int *rows, int *cols) {
    char buf[32];
    unsigned int i = 0;

    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) {
        return -1;
    }

    while (i < sizeof(buf) - 1) {
        if (read(STDIN_FILENO, &buf[i], 1) != 1) {
            break;
        }

        if (buf[i] == 'R') {
            break;
        }
        i++;
    }

    buf[i] = '\0';
    if (buf[0] != '\x1b' || buf[1] != '[') {
        return -1;
    }

    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) {
        return -1;
    }

    return 0;
} 

/**
 * Clears the screen when called
 * This function writes an escape sequence into the terminal, which instruct the terminal to do text formatting tasks
 */ 
void refresh_screen() {
    struct appendBuffer obj = APPENDBUFFER_INIT;

    // Hides cursor while screen refreshes (l means Reset Mode)
    append_to_append_buffer(&obj, "\x1b[?25l", 6);

    // Moves the cursor to the top left of the screen. Uses H command (Cursor Position)
    append_to_append_buffer(&obj, "\x1b[H", 3);

    draw_rows(&obj);

    // Moves cursor to the current position
    char buff[32];
    snprintf(buff, sizeof(buff), "\x1b[%d;%dH", eConfig.cursorY + 1, eConfig.cursorX + 1);
    append_to_append_buffer(&obj, buff, strlen(buff));

    // Makes cursor visible (h means Set Mode)
    append_to_append_buffer(&obj, "\x1b[?25h", 6);
    
    // Write out the buffer's content to the terminal
    write(STDOUT_FILENO, obj.buf, obj.length);
    free_append_buffer(&obj);
}

/**
 * Draws tildes at the left hand side of each of the rows  
 */
void draw_rows(struct appendBuffer *obj) {
    for (int y = 0; y < eConfig.windowRows; y++) {
        // Displays welcome message halfway down the screen
        if (y == eConfig.windowRows/2) {
            char message[80];
            int messageLength = snprintf(message, sizeof(message), "Texto -- version %s", PROGRAM_VERSION);

            if (messageLength > eConfig.windowCols) {
                messageLength = eConfig.windowCols;
            }

            // Centers message
            int padding = (eConfig.windowCols - messageLength) / 2; // Gets amount of space characters required to center message
            if (padding) {
                append_to_append_buffer(obj, "~", 1);
                padding--;
            }

            // Continues centering message
            while (padding--) {
                append_to_append_buffer(obj, " ", 1);
            }

            append_to_append_buffer(obj, message, messageLength);
        } else {
            append_to_append_buffer(obj, "~", 1);
        }

        // \x1b is the escape character (27 in decimal). [0K are the remaining three bytes. We are using the K command (Erase in Line). The 0 says clear to the right of the cursor. 
        append_to_append_buffer(obj, "\x1b[0K", 4);

        if (y < eConfig.windowRows - 1) { // if statement ensures last line has a tilde
            append_to_append_buffer(obj, "\r\n", 2);
        }
    }
}

/**
 * Waits for a key press and then returns it
 */ 
int read_key() {
    int notRead;
    char input;

    while ((notRead = read(STDIN_FILENO, &input, 1)) != 1) { 
        if (notRead == -1 && errno != EAGAIN) {
            safe_exit("read");
        }
    }

    // If input is an escape sequence
    if (input == '\x1b') {
        // Read two more bytes
        char seq[3]; 
        if (read(STDIN_FILENO, &seq[0], 1) != 1) {
            return '\x1b';
        }
        if (read(STDIN_FILENO, &seq[1], 1) != 1) {
            return '\x1b';
        }
        
        // If an arrow key was pressed
        if (seq[0] == '[') {
            switch (seq[1]) {
                case 'A': 
                    return ARROW_UP;
                case 'B': 
                    return ARROW_DOWN;
                case 'C':  
                    return ARROW_LEFT;
                case 'D': 
                    return ARROW_RIGHT;
            }
        }

        return '\x1b';
    } else {
        return input;
    }
}

/**
 * Handles key inputs meant for moving the cursor
 */ 
void move_cursor(int input) {
    switch (input) {
        case ARROW_LEFT:
            eConfig.cursorX--;
            break;
        case ARROW_RIGHT:
            eConfig.cursorX++;
            break;
        case ARROW_UP:
            eConfig.cursorY--;
            break;
        case ARROW_DOWN:
            eConfig.cursorY++;
            break;    
    }
}

/**
 * Waits for a key press and then handles it. 
 */ 
void process_key_press() {
    int input = read_key(); // Gets input

    // Checks if input matches any reserved commands
    switch (input) {
        case CTRL_KEY('q'):
            // Clears screen
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;
        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
            move_cursor(input);
            break;
    }
}

/**
 * Appends a string to the end of the appendBuffer's string.
 */ 
void append_to_append_buffer(struct appendBuffer *obj, const char *buff, int len) {
    // Reallocates the memory allocated to obj's array and makes it the correct size.
    char *newString = realloc(obj->buf, obj->length + len);

    if (newString == NULL) {
        return;
    }

    // Appends the buff string to the end of the obj string
    memcpy(&newString[obj->length], buff, len);

    // Sets parameters
    obj->buf = newString;
    obj->length += len;
}

/**
 * Frees dynamically allocated memory (the string part of the appendBuffer object).
 */ 
void free_append_buffer(struct appendBuffer *obj) {
    // Frees dynamicall
    free(obj->buf);
}



