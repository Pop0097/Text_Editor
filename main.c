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

#define APPEND_BUFFER_INIT {NULL, 0} // Default constructor for appendBuffer structure
#define PROGRAM_VERSION "0.0.1"
#define TAB_STOP 8
#define HL_HIGHLIGHT_NUMBERS (1 << 0)
#define HL_HIGHLIGHT_STRINGS (1 << 1)
#define HLDB_ENTRIES (sizeof(HLDB)/sizeof(HLDB[0])) // stores length of HLDB array

#define QUIT_TIMES 2

// Stores a row of text
typedef struct editorRow {
    int index;
    int highlightOpenComment;
    int rsize;
    char *render; // This contains the text that will be displayed
    int size;
    char *characters;
    unsigned char *highlight;
} editorRow;

// Stores code syntax type
struct editorSyntax {
    char *filetype;
    char **filematch;
    char **keywords;
    char *singlelineCommentStart;
    char *mlCommentStart;
    char *mlCommentEnd;
    int flags;
};

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
    struct editorSyntax *syntax;
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

enum editorHighlight {
    HL_NORMAL = 0,
    HL_NUMBER,
    HL_SEARCH_RESULT,
    HL_STRING,
    HL_COMMENT,
    HL_KEYWORD1, // Primary
    HL_KEYWORD2, // Secondary
    HL_MLCOMMENT // Multi-line comment
};

char *C_HL_extensions[] = {".c", ".h", ".cpp", ".hpp", NULL};
char *C_HL_keywords[] = {
    "switch", "if", "while", "for", "break", "continue", "return", "else", "struct", "union", "typedef", "static", "enum", "class", "case", // Primary keywords
    
    "int|", "long|", "double|", "float|", "char|", "unsigned|", "signed|", "void|", // Secondary keywords
    
    NULL 
};

struct editorSyntax HLDB[] = {
    {
        "c",
        C_HL_extensions,
        C_HL_keywords,
        "//",
        "/*",
        "*/",
        HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS
    }
};

// Functions declared here in the exact order with which they are defined
void init();
void open_file(char*);
char *prompt(char*, void (*func)(char*, int));
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
int row_render_index_to_character_index(editorRow*, int);
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
void update_syntax(editorRow*);
int syntax_to_colour(int);
void select_syntax_highlight();
void save();
void find_callback(char*, int);
void find();
int is_separator(int);

int main(int argc /* Argument count */, char ** argv /* Argument values */) {
    enable_raw_mode(); // Before doing anything else, we must put terminal in correct mode
    init();

    if (argc >= 2) {
        open_file(argv[1]);
    }

    set_status_message("HELP: Ctrl-Q = quit | Ctrl-F = find | Ctrl-S = save");

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
    eConfig.syntax = NULL;

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

    select_syntax_highlight();

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
 * Promps user in message bar and accepts input
 */
char *prompt(char *promp, void (*func)(char*, int)) {
    size_t bufferSize = 128;
    char *buf = malloc(bufferSize);

    size_t bufferLength = 0;
    buf[0] = '\0';

    // Gets user input
    while(1) {
        set_status_message(promp, buf);
        refresh_screen();

        int input = read_key();

        if (input == DELETE_KEY || input == CTRL_KEY('h') || input == BACKSPACE) { 
            if (bufferLength != 0) {
                buf[--bufferLength] = '\0';
            }
        } else if (input == '\x1b') { // Cancel process
            set_status_message("");
            if (func) {
                func(buf, input);
            }
            free(buf);
            return NULL;
        } else if (input == '\r'){ // Enter key is pressed, returns buf
            if (bufferLength != 0) {
                set_status_message("");
                if (func) {
                    func(buf, '\x1b');
                }
                return buf;
            }
            return NULL;
        } else if (!iscntrl(input) && input < 128) { // If user enters key
            if (bufferLength == bufferSize - 1) {
                bufferSize *= 2;
                buf = realloc(buf, bufferSize);
            }
            buf[bufferLength++] = input;
            buf[bufferLength] = '\0';
        }

        if (func) {
            func(buf, input);
        }
    }
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
    for (int i = index + 1; i <= eConfig.numRows; i++) {
        eConfig.row[i].index++;
    }

    eConfig.row[index].index = index;

    eConfig.row[index].size = length;
    eConfig.row[index].characters = malloc(length + 1);
    memcpy(eConfig.row[index].characters, rowValue, length);
    eConfig.row[index].characters[length] = '\0';
    eConfig.numRows++;
    eConfig.unsavedChanges++;

    eConfig.row[index].rsize = 0;
    eConfig.row[index].render = NULL;
    eConfig.row[index].highlight = NULL;
    eConfig.row[index].highlightOpenComment = 0;
    update_row(&eConfig.row[index]);   
}

/**
 * Updates parameters of editorRow object
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

    update_syntax(row);
}

/**
 * Frees all heap-allocated parameters of the editorRow object
 */
void free_row(editorRow *row) {
    free(row->render);
    free(row->characters);
    free(row->highlight);
}

/**
 * Removes row from the array of rows in the editorConfiguration object
 */
void delete_row(int index) {
    if (index < 0 || index > eConfig.numRows) {
        return;
    }

    free_row(&eConfig.row[index]); // Clears buffers in row
    memmove(&eConfig.row[index], &eConfig.row[index + 1], sizeof(editorRow) * (eConfig.numRows - index - 1)); // Move memory of previous row to the recently deleted.
    for (int i = index; i < eConfig.numRows - 1; i++) {
        eConfig.row[i].index--;
    }
    eConfig.numRows--;
    eConfig.unsavedChanges++;
}

/**
 * Used when typing a character
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
 * Used when deleting a row
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
 * Used to delete a character 
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

    struct appendBuffer obj = APPEND_BUFFER_INIT;

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

            char *s = &eConfig.row[fileRow].render[eConfig.colOffset];
            unsigned char *hl = &eConfig.row[fileRow].highlight[eConfig.colOffset];
            int currentColour = -1;
            for (int i = 0; i < length; i++) {
                if (iscntrl(s[i])) { // Handles non-printable characters
                    char sym = (s[i] < 26) ? '@' + s[i] : '?';
                    append_to_append_buffer(obj, "\x1b[7m", 4); // Highlight colour white
                    append_to_append_buffer(obj, &sym, 1);
                    append_to_append_buffer(obj, "\x1b[m", 3); // Set colour back to normal
                    if (currentColour != -1) { // Set colour back to original
                        char buf[16];
                        int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", currentColour); 
                        append_to_append_buffer(obj, buf, clen);
                    }
                } else if (hl[i] == HL_NORMAL) {
                    if (currentColour != -1) { // Only change colour if previous colour is not HL_NORMAL
                        append_to_append_buffer(obj, "\x1b[39m", 5); // Set colour back to normal
                        currentColour = -1;
                    }
                    append_to_append_buffer(obj, &s[i], 1);
                } else {
                    int colour = syntax_to_colour(hl[i]);
                    if (colour != currentColour) { // Only change colour if previous colour is not HL_NORMAL
                        currentColour = colour;
                        char buf[16];
                        int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", colour); // Use snprintf to write escape sequence into buffer that will be passed into append_to_append_buffer()
                        append_to_append_buffer(obj, buf, clen);
                    }
                    append_to_append_buffer(obj, &s[i], 1);
                }
            }
            append_to_append_buffer(obj, "\x1b[39m", 5);
        }

        // \x1b is the escape character (27 in decimal). [0K are the remaining three bytes. We are using the K command (Erase in Line). The 0 says clear to the right of the cursor. 
        append_to_append_buffer(obj, "\x1b[0K", 4);
        append_to_append_buffer(obj, "\r\n", 2);
    }
}

/**
 * Displays status bar at the second last row of window
 */
void draw_status_bar(struct appendBuffer *obj) {
    append_to_append_buffer(obj, "\x1b[7m", 4); // inverts colours (m command is the "Select Graphic Rendition" condition)

    // Prepares string to be printed
    char status[80], renderStatus[80];
    int length = snprintf(status, sizeof(status), "%.20s - %d lines %s", eConfig.fileName ? eConfig.fileName : "[No Name]", eConfig.numRows, eConfig.unsavedChanges != 0 ? "(modified)" : "");
    int renderLength = snprintf(renderStatus, sizeof(renderStatus), "%s | %d/%d", eConfig.syntax ? eConfig.syntax->filetype : "no file type", eConfig.characterY + 1, eConfig.numRows);
    if (length > eConfig.windowCols) {
        length = eConfig.windowCols;
    }

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
 * Displays message bar at the bottom of window
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
 * Converts the index in terms of the render array to the cursor index in ters of the characters array
 */
int row_render_index_to_character_index(editorRow *row, int renderX) {
    int currentRenderX = 0;
    int characterX;
    for (characterX = 0; characterX < row->size; characterX++) {
        if (row->characters[characterX] == '\t') {
            currentRenderX += (TAB_STOP - 1) - (currentRenderX % TAB_STOP);
        }
        currentRenderX++;

        if (currentRenderX > renderX) {
            return characterX;
        }
    }
    return characterX;
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
 * Called when character is typed
 */
void insert_character(int character) {
    if (eConfig.characterY == eConfig.numRows) { // If we are at the end of the file, we need to add a new row.
        insert_row(eConfig.numRows, "", 0);
    }
    insert_character_in_row(&eConfig.row[eConfig.characterY], eConfig.characterX, character);
    eConfig.characterX++;
}

/**
 * Called when newline (ENTER, etc) is pressed
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
 * Called when DELETE, etc. is pressed
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
        
        case CTRL_KEY('f'):
            find();
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
 * Converts all of the rows in the rows parameter of the editorConfiguration object to a string
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
 * Updates the highlight array to contain colours of characters
 */
void update_syntax(editorRow *row) {
    row->highlight = realloc(row->highlight, row->size); // In case row grew in size before last call
    memset(row->highlight, HL_NORMAL, row->rsize); // Sets memory

    if (eConfig.syntax == NULL) {
        return;
    }

    char **keywords = eConfig.syntax->keywords;

    char *scs = eConfig.syntax->singlelineCommentStart;
    char *mcs = eConfig.syntax->mlCommentStart;
    char *mce = eConfig.syntax->mlCommentEnd;

    int scsLen = scs ? strlen(scs) : 0;
    int mcsLen = mcs ? strlen(mcs) : 0;
    int mceLen = mce ? strlen(mce) : 0;
    
    int previousSeparator = 1; // Consider begining of line as separator
    int inString = 0;
    int inComment = (row->index > 0 && eConfig.row[row->index - 1].highlightOpenComment); // True if row has a multiline comment

    int i = 0;
    while (i < row->size) {
        char c = row->render[i];
        unsigned char prevHighlight = (i > 0) ? row->highlight[i - 1] : HL_NORMAL;

        if (scsLen && !inString && !inComment) { // Checks is we are not within quotations
            if (!strncmp(&row->render[i], scs, scsLen)) { // Checks if string is present in line
                memset(&row->highlight[i], HL_COMMENT, row->rsize - i); // Colours
                break; // At end of line so we exit loop
            }
        }

        if (mcsLen && mceLen && !inString) { // Ensures parameters are defined
            if (inComment) { // Sees if we are in comment
                row->highlight[i] = HL_MLCOMMENT; // Sets highlight colour
                if (!strncmp(&row->render[i], mce, mceLen)) { // If we are at the end of the comment
                    memset(&row->highlight[i], HL_MLCOMMENT, mceLen); 
                    i += mceLen;
                    inComment = 0;
                    previousSeparator = 1;
                    continue;
                } else { 
                    i++;
                    continue;
                } 
            } else if (!strncmp(&row->render[i], mcs, mcsLen)) { // If we have reached the start of a ML comment
                memset(&row->highlight[i], HL_MLCOMMENT, mcsLen);
                i += mcsLen;
                inComment = 1;
                continue;
            }
        }

        if (eConfig.syntax->flags & HL_HIGHLIGHT_STRINGS) {
            if (inString) {
                row->highlight[i] = HL_STRING;
                if (c == inString) { // Check if current character is closing quotation
                    inString = 0;
                }
                i++;
                previousSeparator = 1;
                continue;
            } else {
                if (c == '"' || c == '\'' || c == '\"') {
                    inString = c;
                    row->highlight[i] = HL_STRING;
                    i++;
                    continue;
                }
            }
        }
        
        if (eConfig.syntax->flags & HL_HIGHLIGHT_NUMBERS) { // Checks if numbers should be highighted for the current file type
            if (isdigit(c) && (previousSeparator || prevHighlight == HL_NUMBER) || (c == '.' && prevHighlight == HL_NUMBER)) { // If number, use number highlighting
                row->highlight[i] = HL_NUMBER;
                i++;
                previousSeparator = 0;
                continue;
            }
        } 

        if (previousSeparator) { // Check if previous character was a separator
            int j;
            for (j = 0; keywords[j]; j++) { // Go through all available keywords until match is found
                int keywordLen = strlen(keywords[j]);
                
                // Checks if secondary keyword and if so, remove '|'
                int keyword2 = (keywords[j][keywordLen - 1] == '|'); 
                if (keyword2) {
                    keywordLen--;
                }

                if (!strncmp(&row->render[i], keywords[j], keywordLen) /* Does keyword exist? */ && is_separator(row->render[i + keywordLen]) /* Is there a separator directly after keyword */) {
                    memset(&row->highlight[i], keyword2 ? HL_KEYWORD2 : HL_KEYWORD1, keywordLen);
                    i+= keywordLen;
                    break;
                }
            }

            if (keywords[j] != NULL) {
                previousSeparator = 0;
                continue;
            }
        }
        
        previousSeparator = is_separator(c);
        i++;
    }

    // Tells us if multicomment is closed in this row or if it continues
    int changed = (row->highlightOpenComment != inComment);
    row->highlightOpenComment = inComment;
    if (changed && row->index + 1 < eConfig.numRows) { // If comment status changed, update syntax for all rows 
        update_syntax(&eConfig.row[row->index + 1]);
    }
}

/**
 * Returns the colour of a character
 */
int syntax_to_colour(int highlightValue) {
    switch (highlightValue) {
        case HL_NUMBER:
            return 31;

        case HL_SEARCH_RESULT:
            return 34;

        case HL_STRING:
            return 35;

        case HL_COMMENT:
        case HL_MLCOMMENT:
            return 36;

        case HL_KEYWORD1:
            return 33;
        
        case HL_KEYWORD2:
            return 32;

        default:
            return 37;
    }
} 

/**
 * 
 */
void select_syntax_highlight() {
    eConfig.syntax = NULL;

    if (eConfig.fileName == NULL) {
        return;
    }

    char *extension = strrchr(eConfig.fileName, '.'); // Get pointer to extensions part of the filename

    for (int i = 0; i < HLDB_ENTRIES; i++) { // Loop through each editor syntax 
        struct editorSyntax *s = &HLDB[i];
        unsigned int j = 0;
        while (s->filematch[j]) { // Loop through each pattern in filematch array
            int isExtension = (s->filematch[j][0] == '.'); // Checks if file extension exists
            if ((isExtension && extension && !strcmp(extension, s->filematch[j])) || (!isExtension && strstr(eConfig.fileName, s->filematch[j]) /* If no file extension, sees if character sequence appears in filename at all */)) {
                eConfig.syntax = s;

                for (int fileRow = 0; fileRow < eConfig.numRows; fileRow++) {
                    update_syntax(&eConfig.row[fileRow]);
                }

                return;
            }
            j++;
        }
    }
}

/**
 * Saves all changes made to the file onto the disk 
 */  
void save() {
    if (eConfig.fileName == NULL) {
        eConfig.fileName = prompt("Save as: %s (ESC to cancel)", NULL);
        if (eConfig.fileName == NULL) {
            set_status_message("Save canceled");
            return;
        }
        select_syntax_highlight();
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

/**
 * Finds occurences of query
 */
void find_callback(char *query, int key) {
    static int lastMatch = -1;
    static int direction = 1;

    static int savedHighlightLine;
    static char *savedHighlight = NULL;

    if (savedHighlight) { // Restores colours to default
        memcpy(eConfig.row[savedHighlightLine].highlight, savedHighlight, eConfig.row[savedHighlightLine].rsize);
        free(savedHighlight);
        savedHighlight = NULL;
    }

    if (key == '\x1b') { // Pressing enter or escape leaves mode
        lastMatch = -1;
        direction = 1;
        return;
    } else if (key == ARROW_RIGHT || key == ARROW_DOWN) {
        direction = 1;
    } else if (key == ARROW_LEFT || key == ARROW_UP) {
        direction = -1;
    } else {
        lastMatch = -1;
        direction = 1;
    }

    if (lastMatch == -1) {
        direction = 1;
    }
    int current = lastMatch; // Index of the current row we are searching in the "row" parameter

    for (int i = 0; i < eConfig.numRows; i++) {
        current += direction;
        if (current == -1) { // If at top of file, go to bottom
            current = eConfig.numRows - 1;
        } else if (current == eConfig.numRows) { // If at end of file, go to beginning
            current = 0;
        }

        editorRow *row = &eConfig.row[current];

        char *match = strstr(row->render, query); // Finds first occurence of substring (needle [second param]) in the string (haystack [first param])
        if (match) {
            lastMatch = current;
            eConfig.characterY = current;
            eConfig.characterX = row_render_index_to_character_index(row, match - row->render);
            eConfig.rowOffset = current - 10;
            if (current - 10 < 0) {
                eConfig.rowOffset = 0;
            }

            // Before highlighting match we must save the current row
            savedHighlightLine = current;
            savedHighlight = malloc(row->size);
            memcpy(savedHighlight, row->highlight, row->rsize); 
            
            memset(&row->highlight[match - row->render], HL_SEARCH_RESULT, strlen(query)); // Highlights matches

            break;
        }
    }
}

/**
 * Used to handle case when Ctrl-F is pressed
 */
void find() {
    // Save initial cursor location
    int savedCharacterX = eConfig.characterX;
    int savedCharacterY = eConfig.characterY;
    int savedColOff = eConfig.colOffset;
    int savedRowOff = eConfig.rowOffset;

    // Get query
    char *query = prompt("Search %s (ESC to exit | Arrows to navigate)", find_callback);
    
    // If query exists, free it from heap, else restore previous cursor position
    if (query) {
        free(query);
    } else {
        eConfig.characterX = savedCharacterX;
        eConfig.characterY = savedCharacterY;
        eConfig.rowOffset = savedRowOff;
        eConfig.colOffset = savedColOff;
    }

    set_status_message("Exited Search Mode");
}

/**
 * Determines if character separates words
 */
int is_separator(int c) {
    return isspace(c) || c == '\0' || strchr(",.()+-/*=~%<>[];", c) != NULL;
}

