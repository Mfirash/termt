#include "termwidget.h"
#include <thread>
#include <iostream>

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

#ifdef _WIN32
#include <winpty.h>
#endif

termwidget::termwidget() : winpty_instance(nullptr) {
    set_can_focus(true);
    add_events(Gdk::KEY_PRESS_MASK);

    // Initial grid size
    ioterm_init(24, 80);
    
    timeout_conn = Glib::signal_timeout().connect(sigc::mem_fun(*this, &termwidget::on_timeout), 16);
}

termwidget::~termwidget() {
    if (timeout_conn) {
        timeout_conn.disconnect();
    }
}

void termwidget::on_size_allocate(Gtk::Allocation& allocation) {
    Gtk::DrawingArea::on_size_allocate(allocation);

    int new_cols = allocation.get_width() / CHAR_WIDTH;
    int new_rows = allocation.get_height() / CHAR_HEIGHT;

    if (new_cols > 0 && new_rows > 0 && (new_cols != cols || new_rows != rows)) {
        resize(new_rows, new_cols);
        
#ifdef _WIN32
        if (winpty_instance) {
            winpty_set_size(winpty_instance, new_cols, new_rows, nullptr);
        }
#endif
    }
}

bool termwidget::on_draw(const Cairo::RefPtr<Cairo::Context>& cr) {
    std::lock_guard<std::mutex> lock(grid_mutex);
    
    cr->set_source_rgb(0, 0, 0);
    cr->paint();

    // Set font to Consolas
    cr->select_font_face("Consolas", Cairo::FONT_SLANT_NORMAL, Cairo::FONT_WEIGHT_NORMAL);
    cr->set_font_size(14); 

    Cairo::FontExtents fe;
    cr->get_font_extents(fe);

    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            Cell& cell = grid[r][c];

            if (cell.attributes.bg_color != COLOR_DEFAULT) {
                cr->set_source_rgb(0.15, 0.15, 0.15); 
                cr->rectangle(c * CHAR_WIDTH, r * CHAR_HEIGHT, CHAR_WIDTH, CHAR_HEIGHT);
                cr->fill();
            }

            unsigned char ch = static_cast<unsigned char>(cell.character);
            
            // Filter out control chars (0-31) and DEL (127) to hide raw ANSI/Control garbage
            if (ch >= 32 && ch < 127) { 
                cr->set_source_rgb(0.9, 0.9, 0.9); // Off-white for better readability
                
                // Precise alignment:
                // Move to cell start, then add half the remaining space to center vertically
                double y_offset = r * CHAR_HEIGHT + (CHAR_HEIGHT - (fe.ascent + fe.descent)) / 2.0 + fe.ascent;
                cr->move_to(c * CHAR_WIDTH, y_offset);
                
                std::string s(1, (char)ch);
                cr->show_text(s);
            }
        }
    }

    if (cursor_visible && cursor_y >= 0 && cursor_y < rows && cursor_x >= 0 && cursor_x < cols) {
        cr->set_source_rgba(1, 1, 1, 0.5);
        cr->rectangle(cursor_x * CHAR_WIDTH, cursor_y * CHAR_HEIGHT, CHAR_WIDTH, CHAR_HEIGHT);
        cr->fill();
    }

    return true;
}

void termwidget::fork_shell() {
    std::thread([this]() {
        loop = uv_default_loop();
        uv_async_init(loop, &async_write_handle, on_async_write);
        async_write_handle.data = this;

#ifdef _WIN32
        winpty_error_ptr_t err = nullptr;
        winpty_config_t* config = winpty_config_new(0, &err);
        winpty_config_set_initial_size(config, cols, rows);
        
        winpty_instance = winpty_open(config, &err);
        winpty_config_free(config);

        if (!winpty_instance) return;

        HANDLE stdin_h = CreateFileW(winpty_conin_name(winpty_instance), GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
        HANDLE stdout_h = CreateFileW(winpty_conout_name(winpty_instance), GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);

        int stdin_fd = _open_osfhandle((intptr_t)stdin_h, _O_APPEND);
        int stdout_fd = _open_osfhandle((intptr_t)stdout_h, _O_RDONLY);

        uv_pipe_init(loop, &stdin_pipe, 0);
        uv_pipe_open(&stdin_pipe, stdin_fd);

        uv_pipe_init(loop, &stdout_pipe, 0);
        uv_pipe_open(&stdout_pipe, stdout_fd);

        winpty_spawn_config_t* spawn_config = winpty_spawn_config_new(
            WINPTY_SPAWN_FLAG_AUTO_SHUTDOWN,
            L"C:\\Windows\\System32\\cmd.exe",
            NULL, NULL, NULL, &err
        );
        
        winpty_spawn(winpty_instance, spawn_config, NULL, NULL, NULL, &err);
        winpty_spawn_config_free(spawn_config);
        
        uv_read_start((uv_stream_t*)&stdout_pipe, alloc_buffer, on_uv_read);
        uv_run(loop, UV_RUN_DEFAULT);
#endif
    }).detach();
}

void termwidget::alloc_buffer(uv_handle_t*, size_t suggested_size, uv_buf_t* buf) {
    buf->base = new char[suggested_size];
    buf->len = (unsigned int)suggested_size;
}

void termwidget::on_uv_read(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
    if (nread > 0) {
        write(std::string(buf->base, nread)); 
    } else if (nread < 0) {
        uv_read_stop(stream);
    }
    if (buf->base) delete[] buf->base;
}

bool termwidget::on_timeout() {
    queue_draw();
    return true;
}

void termwidget::on_async_write(uv_async_t* handle) {
    termwidget* self = static_cast<termwidget*>(handle->data);
    std::vector<char> to_write;
    {
        std::lock_guard<std::mutex> lock(self->input_mutex);
        if (self->input_buffer.empty()) return;
        to_write.swap(self->input_buffer);
    }

    uv_buf_t buf = uv_buf_init(new char[to_write.size()], (unsigned int)to_write.size());
    std::copy(to_write.begin(), to_write.end(), buf.base);

    uv_write_t* req = new uv_write_t();
    req->data = buf.base;
    uv_write(req, (uv_stream_t*)&self->stdin_pipe, &buf, 1, [](uv_write_t* req, int) {
        delete[] static_cast<char*>(req->data);
        delete req;
    });
}

bool termwidget::on_key_press_event(GdkEventKey* event) {
    std::string seq = "";
    switch (event->keyval) {
        case GDK_KEY_Up:    seq = "\x1b[A"; break;
        case GDK_KEY_Down:  seq = "\x1b[B"; break;
        case GDK_KEY_Right: seq = "\x1b[C"; break;
        case GDK_KEY_Left:  seq = "\x1b[D"; break;
        case GDK_KEY_Return: seq = "\r"; break;
        case GDK_KEY_BackSpace: seq = "\x08"; break;
        case GDK_KEY_Tab:    seq = "\t"; break;
        default:
            if (event->keyval >= 32 && event->keyval <= 126) seq = (char)event->keyval;
            break;
    }
    if (!seq.empty()) {
        std::lock_guard<std::mutex> lock(input_mutex);
        for (char c : seq) input_buffer.push_back(c);
        uv_async_send(&async_write_handle);
    }
    return true;
}