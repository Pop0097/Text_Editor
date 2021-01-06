// Defines feature test macros so code is more portable 
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <stdio.h>
#include <errno.h>
#include <ctype.h>
#include <unistd.h>
#include <termios.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>
#include <fcntl.h>

// Gets ASCII value of Ctrl-k by setting bits 5-7 as 0
#define CTRL_KEY(letter) ((letter) & 0x1f) 

#define APPENDBUFFER_INIT {NULL, 0} // Default constructor for appendBuffer structure
#define PROGRAM_VERSION "0.0.1"
#define TAB_STOP 8

#define QUIT_TIMES 2

// Stores a row of text
typedef struct editorRow {
    int rsize;
    char *render; // This contains the text that will be displayed
    int size;
    char *characters;
} editorRow;  

// Stores the configuration of the terminal and editor
struct editorConfiguration {
    int characterX, characterY;
    int renderX;
    int windowRows, windowCols;
    int numRows;
    int rowOffset, colOffset;
    char *fileName;
    char *statusMessage[80];
    int unsavedChanges;
    time_t statusMessageTime;
    editorRow *row;
    struct termios original_termios; 
};

// This allows us to create our own dynamic string 
struct appendBuffer {
    char *buf;
    int length;
};

struct editorConfiguration eConfig;

enum customKeyValues {
    BACKSPACE = 127,
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    PAGE_UP,
    PAGE_DOWN,
    HOME_KEY,
    END_KEY,
    DELETE_KEY
};

// Functions declared here in the exact order with which they are defined
void init();
void open_file(char*);
void insert_row(int, char*, size_t);
void update_row(editorRow*);
void free_row(editorRow*);
void delete_row(int);
void insert_character_in_row(editorRow*, int, int);
void append_string_in_row(editorRow*, char*, size_t);
void delete_character_in_row(editorRow*, int);
void safe_exit(const char*);
void disable_raw_mode();
void enable_raw_mode();
int get_window_size(int*, int*);
int get_cursor_position(int*, int*);
void refresh_screen();
void set_status_message(const char*, ...);
void draw_rows(struct appendBuffer*);
void draw_status_bar(struct appendBuffer*);
void draw_message_bar(struct appendBuffer*);
int row_character_index_to_render_index(editorRow*, int);
void scroll();
int read_key();
void move_cursor(int);
void insert_character(int);
void insert_new_line();
void delete_character();
void process_key_press();
void append_to_append_buffer(struct appendBuffer*, const char*, int);
void free_append_buffer(struct appendBuffer*);
char *rows_to_string(int*);
void save();

int main(int argc /* Argument count */, char ** argv /* Argument values */) {
    enable_raw_mode(); // Before doing anything else, we must put terminal in correct mode
    init();

    if (argc >= 2) {
        open_file(argv[1]);
    }

    set_status_message("HELP: Ctrl-Q = quit | Ctrl-S = save");

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
    eConfig.characterX = 0; // Horizontal (with respect to characters array)
    eConfig.characterY = 0; // Vertical (with respect to characters array)
    eConfig.renderX = 0; // Position of cursor within render array of row
    eConfig.numRows = 0;
    eConfig.row = NULL;
    eConfig.fileName = NULL;
    eConfig.rowOffset = 0;
    eConfig.colOffset = 0;
    eConfig.statusMessage[0] = '\0';
    eConfig.statusMessageTime = 0;
    eConfig.unsavedChanges = 0; // Tells editor if file is modified

    if (get_window_size(&eConfig.windowRows, &eConfig.windowCols) == -1) {
        safe_exit("get_window_size");
    }

    eConfig.windowRows -= 2;
} 

/**
 * Opens desired file
 */ 
void open_file(char *fileName) {
    // Opens file and checks for success
    FILE *fp =fopen(fileName, "r");
    if (!fp) {
        safe_exit("fopen");
    }
    free(eConfig.fileName);
    eConfig.fileName = strdup(fileName); // strdup() copies given string and allocates memory (assumes that we will free it later)

    // Gets te 
    char *line = NULL;
    size_t lineCap = 0;
    ssize_t lineLen; // ssize_t differs from size_t by being signed. As a result, it can take on a negative if an error occurs.

    // NOTE: getline() is useful for reading lines from a file when we don’t know how much memory to allocate for each line.
    while ((lineLen = getline(&line, &lineCap, fp)) != -1) {
        // Trims file
        while (lineLen > 0 && (line[lineLen - 1] == '\n' || line[lineLen - 1] == '\r')) {
            lineLen--;
        }

        // Adds row to editorConfiguration object
        insert_row(eConfig.numRows, line, lineLen);
    }

    eConfig.unsavedChanges = 0;

    free(line);
    fclose(fp);
}

/**
 * Adds row to the array of rows in the editorConfiguration object.
 */
void insert_row(int index, char *rowValue, size_t length) {
    if (index < 0 || index > eConfig.numRows) {
        return;
    }

    eConfig.row = realloc(eConfig.row, sizeof(editorRow) * (eConfig.numRows + 1));
    memmove(&eConfig.row[index + 1], &eConfig.row[index], sizeof(editorRow) * (eConfig.numRows - index));

    eConfig.row[index].size = length;
    eConfig.row[index].characters = malloc(length + 1);
    memcpy(eConfig.row[index].characters, rowValue, length);
    eConfig.row[index].characters[length] = '\0';
    eConfig.numRows++;
    eConfig.unsavedChanges++;

    eConfig.row[index].rsize = 0;
    eConfig.row[index].render = NULL;
    update_row(&eConfig.row[index]);   
}

/**
 * 
 */ 
void update_row(editorRow *row) {
    // Counts the number of tabs in the line
    int tabCount = 0;
    for (int i = 0; i < row->size; i++) {
        if (row->characters[i] == '\t') {
            tabCount++;
        }
    }

    free(row->render);
    row->render = malloc(row->size + (tabCount * (TAB_STOP - 1)) + 1); // Max size of each tab is 8 bytes. row->size accounts for one of the bytes, so we multiply tabCount by 7.

    // Updates the render 
    int index = 0;
    for (int i = 0; i < row->size; i++) {
        if (row->characters[i] == '\t') { // If there is a tab, render it as multiple spaces
            row->render[index++] = ' ';
            while (index % TAB_STOP != 0) { // Tabs only go up to the next column whose number is divisible by 8
                row->render[index++] = ' ';
            }
        } else {
            row->render[index++] = row->characters[i];
        }
    }

    row->render[index] = '\0';
    row->rsize = index;
}

/**
 * 
 */
void free_row(editorRow *row) {
    free(row->render);
    free(row->characters);
}

/**
 * 
 */
void delete_row(int index) {
    if (index < 0 || index > eConfig.numRows) {
        return;
    }

    free_row(&eConfig.row[index]); // Clears buffers in row
    memmove(&eConfig.row[index], &eConfig.row[index + 1], sizeof(editorRow) * (eConfig.numRows - index - 1)); // Move memory of previous row to the recently deleted.
    eConfig.numRows--;
    eConfig.unsavedChanges++;
}

/**
 * 
 */
void insert_character_in_row(editorRow *row, int index, int character) {
    if (index < 0 || index > row->size) {
        index = row->size;
    }

    row->characters = realloc(row->characters, row->size + 2); // Add two to make room for null byte
    memmove(&row->characters[index + 1], &row->characters[index], row->size - index + 1); // Memmove like memcpy, but is safer to use when memory overlaps
    row->size++;
    row->characters[index] = character;
    update_row(row);

    eConfig.unsavedChanges++;
}

/**
 * 
 */
void append_string_in_row(editorRow *row, char *str, size_t length) {
    row->characters = realloc(row->characters, row->size + length + 1);

    memcpy(&row->characters[row->size], str, length);
    
    row->size += length;
    row->characters[row->size] = '\0';
    
    update_row(row);
    
    eConfig.unsavedChanges++;
} 

/**
 * 
 */
void delete_character_in_row(editorRow *row, int index) {
    if (index < 0 || index >= row->size) {
        return;
    }

    memmove(&row->characters[index] /* Destination */, &row->characters[index + 1] /* Starting address */, row->size - index /* Indices */);
    row->size--;
    update_row(row);
    eConfig.unsavedChanges++;
} 

/**
 * If an error occurs, this function is called and prints the error and then exits
 */ 
void safe_exit(const char *error) {
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
 * Calculates cursor position manually
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
    scroll();

    struct appendBuffer obj = APPENDBUFFER_INIT;

    // Hides cursor while screen refreshes (l means Reset Mode)
    append_to_append_buffer(&obj, "\x1b[?25l", 6);

    // Moves the cursor to the top left of the screen. Uses H command (Cursor Position)
    append_to_append_buffer(&obj, "\x1b[H", 3);

    draw_rows(&obj);
    draw_status_bar(&obj);
    draw_message_bar(&obj);

    // Moves cursor to the current position
    char buff[32];
    snprintf(buff, sizeof(buff), "\x1b[%d;%dH", (eConfig.characterY - eConfig.rowOffset) + 1, (eConfig.renderX - eConfig.colOffset) + 1);
    append_to_append_buffer(&obj, buff, strlen(buff));

    // Makes cursor visible (h means Set Mode)
    append_to_append_buffer(&obj, "\x1b[?25h", 6);
    
    // Write out the buffer's content to the terminal
    write(STDOUT_FILENO, obj.buf, obj.length);
    free_append_buffer(&obj);
}

/**
 * 
 * Function uses an elipsis parameter (makes this function a VARIADIC FUNCTION), meaning that we can have a variable amount of parameters (minimum 1 tho)
 */
void set_status_message(const char *message, ...) { 
    va_list ap;
    va_start(ap, message);

    vsnprintf(eConfig.statusMessage, sizeof(eConfig.statusMessage), message, ap);

    va_end(ap);
    eConfig.statusMessageTime = time(NULL);
}

/**
 * Draws tildes at the left hand side of each of the rows  
 */
void draw_rows(struct appendBuffer *obj) {
    for (int y = 0; y < eConfig.windowRows; y++) {
        int fileRow = y + eConfig.rowOffset; // Add offset so we get the lines we wish to see

        // Displays message halfway down the screen after file is displayed
        if (fileRow >= eConfig.numRows) { 
            if (eConfig.numRows == 0 && y == eConfig.windowRows/2) {
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
        } else { // Displays file contents
            int length = eConfig.row[fileRow].rsize - eConfig.colOffset;
            if (length < 0) {
                length = 0;
            }

            if (length > eConfig.windowCols) {
                length = eConfig.windowCols;
            }
            append_to_append_buffer(obj, &eConfig.row[fileRow].render[eConfig.colOffset], length);
        }

        // \x1b is the escape character (27 in decimal). [0K are the remaining three bytes. We are using the K command (Erase in Line). The 0 says clear to the right of the cursor. 
        append_to_append_buffer(obj, "\x1b[0K", 4);
        append_to_append_buffer(obj, "\r\n", 2);
    }
}

/**
 * 
 */
void draw_status_bar(struct appendBuffer *obj) {
    append_to_append_buffer(obj, "\x1b[7m", 4); // inverts colours (m command is the "Select Graphic Rendition" condition)

    // Prepares string to be printed
    char status[80], renderStatus[80];
    int length = snprintf(status, sizeof(status), "%.20s - %d lines %s", eConfig.fileName ? eConfig.fileName : "[No Name]", eConfig.numRows, eConfig.unsavedChanges != 0 ? "(modified)" : "");
    if (length > eConfig.windowCols) {
        length = eConfig.windowCols;
    }

    int renderLength = snprintf(renderStatus, sizeof(renderStatus), "%d:%d", eConfig.characterY + 1, eConfig.numRows);

    // Adds string
    append_to_append_buffer(obj, status, length);

    while (length < eConfig.windowCols) { // Fills in rest of row with white spaces until the point where renderStatus barely fits
        if (eConfig.windowCols - length == renderLength) { 
            append_to_append_buffer(obj, renderStatus, renderLength);
            break;
        } else {
            append_to_append_buffer(obj, " ", 1);
            length++;
        }
    }

    append_to_append_buffer(obj, "\x1b[0m", 4); // Switches back to normal formatting
    append_to_append_buffer(obj, "\r\n", 2);
} 

/**
 * 
 */
void draw_message_bar(struct appendBuffer *obj) {
    append_to_append_buffer(obj, "\x1b[K", 3); // Clears message bar
    
    int messageLength = strlen(eConfig.statusMessage);

    if (messageLength > eConfig.windowCols) {
        messageLength = eConfig.windowCols;
    }

    // After five seconds the message disappears
    if (messageLength && time(NULL) - eConfig.statusMessageTime < 5) { 
        append_to_append_buffer(obj, eConfig.statusMessage, messageLength);
    }
}

/**
 * Converts the cursor index in terms of the characters array to an index in terms of the render array
 */
int row_character_index_to_render_index(editorRow *row, int characterX) {
    int renderX = 0;
    
    for (int i = 0; i < characterX; i++) {
        if (row->characters[i] == '\t') { 
            // (renderX % TAB_STOP) gives us the number of columns between the renderX and the previous tab stop
            // (TAB_STOP - 1) is the maximum we are away from the next tab stop
            // The math is straightforward after this
            renderX += (TAB_STOP - 1) - (renderX % TAB_STOP);
        }
        renderX++; // Gets us to the right of the next tab stop if a tab did exist
    }

    return renderX;
}

/**
 * Scrolls screen if cursor exits the window from top or bottom
 */ 
void scroll() {
    eConfig.renderX = 0;

    if (eConfig.characterY < eConfig.numRows) { // Above visuble window
        eConfig.renderX = row_character_index_to_render_index(&eConfig.row[eConfig.characterY], eConfig.characterX);
    }

    if (eConfig.characterY >= eConfig.rowOffset + eConfig.windowRows) { // Below visible window
        eConfig.rowOffset = eConfig.characterY - eConfig.windowRows + 1;
    }

    // To left of screen
    if (eConfig.renderX < eConfig.colOffset) {
        eConfig.colOffset = eConfig.renderX;
    }

    if (eConfig.renderX >= eConfig.colOffset + eConfig.windowCols) {
        eConfig.colOffset = eConfig.renderX - eConfig.windowCols + 1;
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
        
        
        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                // Reads one more byte
                if (read(STDIN_FILENO, &seq[2], 1) != 1) {
                    return '\x1b';
                }

                if (seq[2] == '~') {
                    switch (seq[1]) {
                        case '1':
                            return HOME_KEY;
                        case '3':
                            return DELETE_KEY;
                        case '4':
                            return END_KEY;
                        case '5':
                            return PAGE_UP;
                        case '6':
                            return PAGE_DOWN;
                        case '7':
                            return HOME_KEY;
                        case '8':
                            return END_KEY;
                    }
                } 
            } else if (seq[0] == 'O') {
                switch (seq[1]) {
                    case 'H':
                        return HOME_KEY;
                    case 'F':
                        return END_KEY;
                }
            } else { // If an arrow key was pressed
                switch (seq[1]) {
                    case 'A': 
                        return ARROW_UP;
                    case 'B': 
                        return ARROW_DOWN;
                    case 'C':  
                        return ARROW_RIGHT;
                    case 'D': 
                        return ARROW_LEFT;
                    case 'H':
                        return HOME_KEY;
                    case 'F':
                        return END_KEY;
                }
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
    editorRow *row = (eConfig.characterY >= eConfig.numRows) ? NULL : &eConfig.row[eConfig.characterY];

    switch (input) {
        case ARROW_LEFT:
            if (eConfig.characterX != 0) {
                eConfig.characterX--;
            } else if (eConfig.characterY > 0) { 
                eConfig.characterY--;
                eConfig.characterX = eConfig.row[eConfig.characterY].size;
            }
            break;
        case ARROW_RIGHT:
            if (row && eConfig.characterX < row->size) {
                eConfig.characterX++;
            } else if (row && eConfig.characterX == row->size) {
                eConfig.characterY++;
                eConfig.characterX = 0;
            }
            break;
        case ARROW_UP:
            if (eConfig.characterY != 0) {
                eConfig.characterY--;
            }
            break;
        case ARROW_DOWN:
            if (eConfig.characterY < eConfig.numRows) {
                eConfig.characterY++;
            }
            break;    
    }

    // If cursor goes past end of line, then this moves it back to the end of the line
    row = (eConfig.characterY >= eConfig.numRows) ? NULL : &eConfig.row[eConfig.characterY];
    int rowLen = row ? row->size : 0;
    if (eConfig.characterX > rowLen) {
        eConfig.characterX = rowLen;
    }
}

/**
 * 
 */
void insert_character(int character) {
    if (eConfig.characterY == eConfig.numRows) { // If we are at the end of the file, we need to add a new row.
        insert_row(eConfig.numRows, "", 0);
    }
    insert_character_in_row(&eConfig.row[eConfig.characterY], eConfig.characterX, character);
    eConfig.characterX++;
}

/**
 * 
 */
void insert_new_line() {
    if (eConfig.characterX == 0) { // If we are at begining of line, insert blank lie before row we were on
        insert_row(eConfig.characterY, "", 0);
    } else { // Otherwise, split line we are on into two rows
        editorRow *row = &eConfig.row[eConfig.characterY];
        insert_row(eConfig.characterY + 1, &row->characters[eConfig.characterX], row->size - eConfig.characterX);
        row = &eConfig.row[eConfig.characterY];
        row->size = eConfig.characterX;
        row->characters[row->size] = '\0';
        update_row(row);
    }
    eConfig.characterY++;
    eConfig.characterX = 0;
}

/**
 * 
 */
void delete_character() {
    if (eConfig.characterY == eConfig.numRows) { // Cursor past end of file, nothing to delete
        return;
    }

    if (eConfig.characterX == 0 && eConfig.characterY == 0) {
        return;
    }

    editorRow *row = &eConfig.row[eConfig.characterY];
    if (eConfig.characterX > 0) {
        delete_character_in_row(row, eConfig.characterX - 1);
        eConfig.characterX--; // Move cursor one to left after deleting
    } else {
        eConfig.characterX = eConfig.row[eConfig.characterY - 1].size;
        append_string_in_row(&eConfig.row[eConfig.characterY] - 1, row->characters, row->size);
        delete_row(eConfig.characterY);
        eConfig.characterY--;
    }
}

/**
 * Waits for a key press and then handles it. 
 */ 
void process_key_press() {
    static int quitTimes = QUIT_TIMES; // Use static variable so value is consistent every time we call this function (in subsequent calls, variable is not re-initialized)

    int input = read_key(); // Gets input

    // Checks if input matches any reserved commands
    switch (input) {
        case '\r': // Enter key
            insert_new_line();
            break;

        case CTRL_KEY('q'):
            if (eConfig.unsavedChanges != 0 && quitTimes > 0) {
                set_status_message("File has unsaved changes. Press Ctrl-Q %d more times to quit.", quitTimes);
                quitTimes--;
                return;
            }
            // Clears screen
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;

        case CTRL_KEY('s'):
            save();
            break;
        
        case BACKSPACE:
        case CTRL_KEY('h'):
        case DELETE_KEY:
            if (input == DELETE_KEY) {
                move_cursor(ARROW_RIGHT);
            }
            delete_character();
            break;

        // Move a single space
        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
            move_cursor(input);
            break;

        // Move to top or bottom of window
        case PAGE_UP:   
        case PAGE_DOWN:
            { // Create code block so we can declare variables
                if (input == PAGE_UP) { // Moves cursor to top of window
                    eConfig.characterY = eConfig.rowOffset;
                } else if (input == PAGE_DOWN) { // Moves cursor to bottom of window
                    eConfig.characterY = eConfig.rowOffset + eConfig.windowRows - 1;
                    if (eConfig.characterY > eConfig.numRows) { // Ensures that the index is not larger than the max number
                        eConfig.characterY = eConfig.numRows;
                    }
                }

                int times = eConfig.windowRows;
                while (times--) {
                    move_cursor(input == PAGE_UP ? ARROW_UP : ARROW_DOWN);
                }
            }
            break;

        // Move cursor to ends of line
        case HOME_KEY: // Begining
            eConfig.characterX = 0;
            break;
        case END_KEY: // End
            if (eConfig.characterY < eConfig.numRows) {
                eConfig.characterX = eConfig.row[eConfig.characterY].size;
            }
            break;

        case CTRL_KEY('l'):
        case '\x1b':
            break;
        
        default:
            insert_character(input);
            break;
    }

    quitTimes = QUIT_TIMES; // If Ctrl-Q is not pressed, value resets
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

/**
 * 
 */
char *rows_to_string(int *bufferLength) {
    int totalLength = 0;
    
    // Add up lengths of each row (we add one for the newline character)
    for (int i = 0; i < eConfig.numRows; i++) {
        totalLength += eConfig.row[i].size + 1;
    }
    *bufferLength = totalLength;

    // Create a buffer with correct length
    char *buf = malloc(totalLength);
    char *p = buf;

    // In each iteration, we copy contents of the row and add a newline character
    for (int i = 0; i < eConfig.numRows; i++)  {
        memcpy(p, eConfig.row[i].characters, eConfig.row[i].size);
        p += eConfig.row[i].size;
        *p = '\n';
        p++; // next memory address
    }

    return buf;
} 

/**
 *
 */  
void save() {
    if (eConfig.fileName == NULL) {
        return;
    }

    int length;
    char *buf = rows_to_string(&length);

    // O_CREATE -> create file if it does not exist
    // O_RDWR -> open file for reading and writing
    // 0644 -> standard permissions (reading and writing)
    int fd = open(eConfig.fileName, O_RDWR | O_CREAT, 0644);

    if (fd != -1) {
        if (ftruncate(fd, length) /* Sets file size to specified length */ != -1) {
            if (write(fd, buf, length) == length) {
                close(fd);
                free(buf);
                set_status_message("%d bytes written to disk", length);
                eConfig.unsavedChanges = 0;
                return;
            }
        }
        close(fd);
    }

    free(buf);
    set_status_message("Can't save to disk! I/O error: %s", strerror(errno));
}

