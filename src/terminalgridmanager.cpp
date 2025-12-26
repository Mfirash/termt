#include "terminalgridmanager.h"
#include <algorithm>
#include <iostream>
#include <sstream>
#include <mutex>

int rows = 0;
int cols = 0;
std::vector<std::vector<Cell>> grid;
int cursor_x = 0;
int cursor_y = 0;
bool cursor_visible = true;
const int CHAR_WIDTH = 9;
const int CHAR_HEIGHT = 18;
std::mutex grid_mutex;
bool tui_mode_active = false;
CellAttributes current_attributes;

void clamp_cursor_internal()
{
    if (cols > 0)
        cursor_x = std::max(0, std::min(cursor_x, cols - 1));
    else
        cursor_x = 0;

    if (rows > 0)
        cursor_y = std::max(0, std::min(cursor_y, rows - 1));
    else
        cursor_y = 0;
}

void scrollup_internal()
{
    if (rows > 1)
    {
        for (int i = 0; i < rows - 1; ++i)
        {
            grid[i] = std::move(grid[i + 1]);
        }
        grid[rows - 1].assign(cols, {' ', CellAttributes{}});
        cursor_y = rows - 1;
    }
}

void ioterm_init(int initial_rows, int initial_cols)
{
    std::lock_guard<std::mutex> lock(grid_mutex);
    rows = initial_rows;
    cols = initial_cols;
    grid.assign(rows, std::vector<Cell>(cols, {' ', CellAttributes{}}));
}

void resize(int new_rows, int new_cols)
{
    std::lock_guard<std::mutex> lock(grid_mutex);
    if (new_rows <= 0 || new_cols <= 0)
        return;
    if (new_rows == rows && new_cols == cols)
        return;
    std::vector<std::vector<Cell>> new_grid(new_rows, std::vector<Cell>(new_cols, {' ', CellAttributes{}}));
    int copy_rows = std::min(rows, new_rows);
    int copy_cols = std::min(cols, new_cols);

    for (int r = 0; r < copy_rows; ++r)
    {
        for (int c = 0; c < copy_cols; ++c)
        {
            new_grid[r][c] = grid[r][c];
        }
    }

    rows = new_rows;
    cols = new_cols;
    grid = std::move(new_grid);
    clamp_cursor_internal();
}

void handle_csi(char command, const std::string &sequence)
{
    bool is_private = (!sequence.empty() && sequence[0] == '?');
    std::string numeric_part = is_private ? sequence.substr(1) : sequence;
    std::vector<int> params;
    std::stringstream ss(numeric_part);
    std::string item;
    while (std::getline(ss, item, ';'))
    {
        try
        {
            if (!item.empty())
                params.push_back(std::stoi(item));
        }
        catch (...)
        {
        }
    }
    if (params.empty())
        params.push_back(0);
    if (is_private)
    {
        switch (command)
        {
        case 'h':
            if (params[0] == 25)
                cursor_visible = true;
            break;
        case 'l':
            if (params[0] == 25)
                cursor_visible = false;
            break;
        }
        return;
    }
    switch (command)
    {
    case 'm':
        for (int p : params)
        {
            if (p == 0)
            {
                current_attributes = CellAttributes{};
            }
            else if (p >= 30 && p <= 37)
            {
                current_attributes.fg_color = (AnsiColor)(p - 30);
            }
            else if (p >= 40 && p <= 47)
            {
                current_attributes.bg_color = (AnsiColor)(p - 40);
            }
            else if (p == 1)
            {
                current_attributes.bold = true;
            }
        }
        break;
    case 'H':
    case 'f':
        cursor_y = (params.size() > 0) ? params[0] - 1 : 0;
        cursor_x = (params.size() > 1) ? params[1] - 1 : 0;
        clamp_cursor_internal();
        break;
    case 'J':
        if (params[0] == 2)
        {
            for (auto &row : grid)
            {
                std::fill(row.begin(), row.end(), Cell{' ', current_attributes});
            }
            cursor_x = 0;
            cursor_y = 0;
        }
        break;
    case 'K':
        if (cursor_y >= 0 && cursor_y < rows)
        {
            int start = std::max(0, std::min(cursor_x, cols));
            std::fill(grid[cursor_y].begin() + start, grid[cursor_y].end(), Cell{' ', current_attributes});
        }
        break;
    case 'A':
        cursor_y -= params[0] ? params[0] : 1;
        clamp_cursor_internal();
        break;
    case 'B':
        cursor_y += params[0] ? params[0] : 1;
        clamp_cursor_internal();
        break;
    case 'C':
        cursor_x += params[0] ? params[0] : 1;
        clamp_cursor_internal();
        break;
    case 'D':
        cursor_x -= params[0] ? params[0] : 1;
        clamp_cursor_internal();
        break;
    }
}

void ioterm_write(const std::string &data)
{
    std::lock_guard<std::mutex> lock(grid_mutex);

    enum State
    {
        TEXT,
        ESCAPE,
        CSI
    };
    static State state = TEXT;
    static std::string sequence;

    for (unsigned char c : data)
    {
        if (state == TEXT)
        {
            if (c == '\x1b')
            {
                state = ESCAPE;
                sequence.clear();
                continue;
            }

            if (c == '\r')
            {
                cursor_x = 0;
            }
            else if (c == '\n')
            {
                cursor_y++;
            }
            else if (c == '\b')
            {
                if (cursor_x > 0)
                    cursor_x--;
            }
            else if (c == '\t')
            {
                cursor_x = (cursor_x / 8 + 1) * 8;
            }
            else if (c >= 32 && c < 127)
            {
                if (cursor_x >= cols)
                {
                    cursor_x = 0;
                    cursor_y++;
                }

                while (cursor_y >= rows)
                {
                    scrollup_internal();
                }

                if (cursor_y >= 0 && cursor_y < rows && cursor_x >= 0 && cursor_x < cols)
                {
                    grid[cursor_y][cursor_x] = {(char)c, current_attributes};
                    cursor_x++;
                }
            }
        }
        else if (state == ESCAPE)
        {
            if (c == '[')
                state = CSI;
            else
                state = TEXT;
            continue;
        }
        else if (state == CSI)
        {
            if (isdigit(c) || c == ';' || c == '?')
            {
                sequence += (char)c;
            }
            else
            {
                handle_csi((char)c, sequence);
                state = TEXT;
            }
            continue;
        }
        if (cursor_x >= cols)
        {
            cursor_x = 0;
            cursor_y++;
        }
        while (cursor_y >= rows)
        {
            scrollup_internal();
        }
    }
}

int get_rows()
{
    std::lock_guard<std::mutex> lock(grid_mutex);
    return rows;
}
int get_cols()
{
    std::lock_guard<std::mutex> lock(grid_mutex);
    return cols;
}
int get_cursor_x()
{
    std::lock_guard<std::mutex> lock(grid_mutex);
    return cursor_x;
}
int get_cursor_y()
{
    std::lock_guard<std::mutex> lock(grid_mutex);
    return cursor_y;
}
bool is_cursor_visible()
{
    std::lock_guard<std::mutex> lock(grid_mutex);
    return cursor_visible;
}
Cell get_cell(int r, int c)
{
    std::lock_guard<std::mutex> lock(grid_mutex);
    if (r >= 0 && r < rows && c >= 0 && c < cols)
        return grid[r][c];
    return {' ', CellAttributes{}};
}

void hidecursor()
{
    std::lock_guard<std::mutex> lock(grid_mutex);
    cursor_visible = false;
}
void showcursor()
{
    std::lock_guard<std::mutex> lock(grid_mutex);
    cursor_visible = true;
}
