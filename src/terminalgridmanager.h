#pragma once
#include <vector>
#include <string>
#include <mutex>

enum AnsiColor {
COLOR_BLACK = 0,
COLOR_RED,
COLOR_GREEN,
COLOR_YELLOW,
COLOR_BLUE,
COLOR_MAGENTA,
COLOR_CYAN,
COLOR_WHITE,
COLOR_DEFAULT = 9
};

struct CellAttributes {
AnsiColor fg_color = COLOR_DEFAULT;
AnsiColor bg_color = COLOR_DEFAULT;
bool bold = false;
};

struct Cell {
char character = ' ';
CellAttributes attributes;
};

extern int rows;
extern int cols;
extern std::vector<std::vector<Cell>> grid;
extern int cursor_x;
extern int cursor_y;
extern bool cursor_visible;
extern const int CHAR_WIDTH;
extern const int CHAR_HEIGHT;
extern CellAttributes current_attributes;
extern std::mutex grid_mutex;
extern const int CHAR_HEIGHT;
extern CellAttributes current_attributes;
extern std::mutex grid_mutex; 
extern bool tui_mode_active;

void ioterm_init(int initial_rows, int initial_cols);
int get_rows();
int get_cols();
int get_cursor_x();
int get_cursor_y();
bool is_cursor_visible();
Cell get_cell(int r, int c);

void resize(int new_rows, int new_cols);
void clear();
void scrollup();
void scrolldown();

void cursorcoord(int X, int Y);
void char_input(char c);
void hidecursor();
void showcursor();

void ioterm_write(const std::string& text);
void parse_sgr_parameter(int param);
void handle_csi(char command, const std::string& sequence);
