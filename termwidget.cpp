#include "termwidget.h"
#include <thread>
#include <iostream>
#include <algorithm>
#include <string>

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#include <winpty.h>
#include <windows.h>
#endif

termwidget::termwidget() : winpty_instance(nullptr),
                           is_selecting(false),
                           sel_start_x(-1), sel_start_y(-1),
                           sel_end_x(-1), sel_end_y(-1)
{
    set_can_focus(true);

    add_events(Gdk::KEY_PRESS_MASK |
               Gdk::BUTTON_PRESS_MASK |
               Gdk::BUTTON_RELEASE_MASK |
               Gdk::POINTER_MOTION_MASK);

    ioterm_init(24, 80);

    loop = uv_default_loop();
    uv_async_init(loop, &async_write_handle, &termwidget::on_async_write);
    async_write_handle.data = this;

    timeout_conn = Glib::signal_timeout().connect(sigc::mem_fun(*this, &termwidget::on_timeout), 16);
}

termwidget::~termwidget()
{
    if (timeout_conn)
        timeout_conn.disconnect();
    if (winpty_instance)
        winpty_free(winpty_instance);
}

void termwidget::fork_shell(const std::string &aa)
{
    std::thread([this, aa]()
                { 
        winpty_error_ptr_t err = nullptr;
        winpty_config_t* config = winpty_config_new(0, &err);
        if (!config) return;
        winpty_config_set_initial_size(config, 80, 24);
        winpty_instance = winpty_open(config, &err);
        winpty_config_free(config);
        if (!winpty_instance) return;
        LPCWSTR conin_name = winpty_conin_name(winpty_instance);
        LPCWSTR conout_name = winpty_conout_name(winpty_instance);
        uv_pipe_init(loop, &stdin_pipe, 0);
        uv_pipe_init(loop, &stdout_pipe, 0);
        HANDLE hIn = CreateFileW(conin_name, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        HANDLE hOut = CreateFileW(conout_name, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
        int stdin_fd = _open_osfhandle((intptr_t)hIn, _O_WRONLY);
        int stdout_fd = _open_osfhandle((intptr_t)hOut, _O_RDONLY);
        uv_pipe_open(&stdin_pipe, stdin_fd);
        uv_pipe_open(&stdout_pipe, stdout_fd);
        int wchars_num = MultiByteToWideChar(CP_UTF8, 0, aa.c_str(), -1, NULL, 0);
        std::wstring wide_aa(wchars_num, 0);
        MultiByteToWideChar(CP_UTF8, 0, aa.c_str(), -1, &wide_aa[0], wchars_num);        
        LPCWSTR cmd = wide_aa.c_str(); 
        winpty_spawn_config_t* spawn_config = winpty_spawn_config_new(
            WINPTY_SPAWN_FLAG_AUTO_SHUTDOWN, 
            cmd,  
            NULL, 
            NULL, 
            NULL, 
            &err
        );
        
        winpty_spawn(winpty_instance, spawn_config, NULL, NULL, NULL, &err);
        winpty_spawn_config_free(spawn_config);

        stdout_pipe.data = this;
        uv_read_start((uv_stream_t*)&stdout_pipe, termwidget::alloc_buffer, termwidget::on_uv_read);

        uv_run(loop, UV_RUN_DEFAULT); })
        .detach();
}

void set_cairo_color(const Cairo::RefPtr<Cairo::Context> &cr, AnsiColor color, bool is_foreground)
{
    switch (color)
    {
    case COLOR_BLACK:
        cr->set_source_rgb(0.0, 0.0, 0.0);
        break;
    case COLOR_RED:
        cr->set_source_rgb(0.8, 0.0, 0.0);
        break;
    case COLOR_GREEN:
        cr->set_source_rgb(0.0, 0.8, 0.0);
        break;
    case COLOR_YELLOW:
        cr->set_source_rgb(0.8, 0.8, 0.0);
        break;
    case COLOR_BLUE:
        cr->set_source_rgb(0.0, 0.0, 0.8);
        break;
    case COLOR_MAGENTA:
        cr->set_source_rgb(0.8, 0.0, 0.8);
        break;
    case COLOR_CYAN:
        cr->set_source_rgb(0.0, 0.8, 0.8);
        break;
    case COLOR_WHITE:
        cr->set_source_rgb(0.8, 0.8, 0.8);
        break;
    case COLOR_DEFAULT:
        if (is_foreground)
            cr->set_source_rgb(0.9, 0.9, 0.9);
        else
            cr->set_source_rgb(0.05, 0.05, 0.05);
        break;
    }
}

bool termwidget::on_draw(const Cairo::RefPtr<Cairo::Context> &cr) 
{
    cr->set_source_rgb(0.05, 0.05, 0.05);
    cr->paint();
    std::lock_guard<std::mutex> lock(grid_mutex);
    cr->select_font_face("Consolas", Cairo::FONT_SLANT_NORMAL, Cairo::FONT_WEIGHT_NORMAL);
    cr->set_font_size(14);
    for (int r = 0; r < rows; ++r)
    {
        for (int c = 0; c < cols; ++c)
        {
            Cell cell = grid[r][c];
            if (cell.attributes.bg_color != COLOR_DEFAULT)
            {
                set_cairo_color(cr, cell.attributes.bg_color, false);
                cr->rectangle(c * CHAR_WIDTH, r * CHAR_HEIGHT, CHAR_WIDTH, CHAR_HEIGHT);
                cr->fill();
            }
            if (cell.character != ' ' && cell.character != '\0')
            {
                set_cairo_color(cr, cell.attributes.fg_color, true);
                if (cell.attributes.bold)
                {
                    cr->select_font_face("Consolas", Cairo::FONT_SLANT_NORMAL, Cairo::FONT_WEIGHT_BOLD);
                }
                else
                {
                    cr->select_font_face("Consolas", Cairo::FONT_SLANT_NORMAL, Cairo::FONT_WEIGHT_NORMAL);
                }

                cr->move_to(c * CHAR_WIDTH, (r + 1) * CHAR_HEIGHT - 4); 
                char str[2] = { cell.character, '\0' };
                cr->show_text(str);
            }
        }
    }
    if (cursor_visible && cursor_x >= 0 && cursor_x < cols && cursor_y >= 0 && cursor_y < rows)
    {
        cr->set_source_rgba(0.0, 0.8, 0.0, 0.5);
        cr->rectangle(cursor_x * CHAR_WIDTH, cursor_y * CHAR_HEIGHT, CHAR_WIDTH, CHAR_HEIGHT);
        cr->fill();
    }

    return true;
}

void termwidget::on_uv_read(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf)
{
    if (nread > 0)
    {
        ioterm_write(std::string(buf->base, nread));
    }
    else if (nread < 0)
    {
        uv_read_stop(stream);
    }
    if (buf->base)
        delete[] buf->base;
}

void termwidget::alloc_buffer(uv_handle_t *, size_t suggested_size, uv_buf_t *buf)
{
    buf->base = new char[suggested_size];
    buf->len = (unsigned int)suggested_size;
}

bool termwidget::on_timeout()
{
    queue_draw();
    return true;
}

void termwidget::on_async_write(uv_async_t *handle)
{
    termwidget *self = static_cast<termwidget *>(handle->data);
    if (!self)
        return;

    std::vector<char> to_write;
    {
        std::lock_guard<std::mutex> lock(self->input_mutex);
        if (self->input_buffer.empty())
            return;
        to_write.swap(self->input_buffer);
    }

    // Allocate persistent buffer for the async write operation
    char *raw_buf = new char[to_write.size()];
    std::copy(to_write.begin(), to_write.end(), raw_buf);
    uv_buf_t buf = uv_buf_init(raw_buf, (unsigned int)to_write.size());

    uv_write_t *req = new uv_write_t();
    req->data = raw_buf; // Store pointer to delete later

    uv_write(req, (uv_stream_t *)&self->stdin_pipe, &buf, 1, [](uv_write_t *req, int status)
             {
        delete[] static_cast<char*>(req->data);
        delete req; });
}

void termwidget::on_size_allocate(Gtk::Allocation &allocation)
{
    Gtk::DrawingArea::on_size_allocate(allocation);
    int new_cols = std::max(1, allocation.get_width() / CHAR_WIDTH);
    int new_rows = std::max(1, allocation.get_height() / CHAR_HEIGHT);
    resize(new_rows, new_cols);

    if (winpty_instance)
    {
        winpty_set_size(winpty_instance, new_cols, new_rows, NULL);
    }
}

bool termwidget::on_key_press_event(GdkEventKey *event)
{
    std::string input;

    switch (event->keyval)
    {
    case GDK_KEY_Return:
        input = "\r\n";
        break;
    case GDK_KEY_BackSpace:
        input = "\b";
        break;
    case GDK_KEY_Tab:
        input = "\t";
        break;
    case GDK_KEY_Escape:
        input = "\x1b";
        break;
    case GDK_KEY_Up:
        input = "\x1b[A";
        break;
    case GDK_KEY_Down:
        input = "\x1b[B";
        break;
    case GDK_KEY_Right:
        input = "\x1b[C";
        break;
    case GDK_KEY_Left:
        input = "\x1b[D";
        break;
    case GDK_KEY_Home:
        input = "\x1b[H";
        break;
    case GDK_KEY_End:
        input = "\x1b[F";
        break;
    case GDK_KEY_Insert:
        input = "\x1b[2~";
        break;
    case GDK_KEY_Delete:
        input = "\x1b[3~";
        break;
    case GDK_KEY_Page_Up:
        input = "\x1b[5~";
        break;
    case GDK_KEY_Page_Down:
        input = "\x1b[6~";
        break;
    case GDK_KEY_F1:
        input = "\x1bOP";
        break;
    case GDK_KEY_F2:
        input = "\x1bOQ";
        break;
    case GDK_KEY_F3:
        input = "\x1bOR";
        break;
    case GDK_KEY_F4:
        input = "\x1bOS";
        break;
    case GDK_KEY_F5:
        input = "\x1b[15~";
        break;
    case GDK_KEY_F6:
        input = "\x1b[17~";
        break;
    case GDK_KEY_F7:
        input = "\x1b[18~";
        break;
    case GDK_KEY_F8:
        input = "\x1b[19~";
        break;
    case GDK_KEY_F9:
        input = "\x1b[20~";
        break;
    case GDK_KEY_F10:
        input = "\x1b[21~";
        break;
    case GDK_KEY_F11:
        input = "\x1b[23~";
        break;
    case GDK_KEY_F12:
        input = "\x1b[24~";
        break;

    default:
        if (event->string && event->length > 0)
        {
            input = event->string;
        }
        break;
    }

    if (!input.empty())
    {
        std::lock_guard<std::mutex> lock(input_mutex);
        for (char c : input)
            input_buffer.push_back(c);
        uv_async_send(&async_write_handle);
    }
    return true;
}

bool termwidget::on_button_press_event(GdkEventButton *event) { return true; }
bool termwidget::on_button_release_event(GdkEventButton *event) { return true; }
bool termwidget::on_motion_notify_event(GdkEventMotion *event) { return true; }