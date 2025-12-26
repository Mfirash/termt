#pragma once
#include <gtkmm/drawingarea.h>
#include <glibmm/main.h>
#include <uv.h>
#include <vector>
#include <string>
#include <mutex>
#include "terminalgridmanager.h"

#ifdef _WIN32
#include <winpty.h>
#endif

class termwidget : public Gtk::DrawingArea {
public:
    termwidget();
    virtual ~termwidget();
    void fork_shell(const std::string& aa);

protected:
    // Overrides
    bool on_draw(const Cairo::RefPtr<Cairo::Context>& cr) override;
    void on_size_allocate(Gtk::Allocation& allocation) override;
    bool on_key_press_event(GdkEventKey* event) override;
    bool on_button_press_event(GdkEventButton* event) override;
    bool on_button_release_event(GdkEventButton* event) override;
    bool on_motion_notify_event(GdkEventMotion* event) override;

    // Callbacks
    bool on_timeout();
    static void on_uv_read(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf);
    static void alloc_buffer(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf);
    static void on_async_write(uv_async_t* handle);

private:
    // libuv and winpty members
    uv_loop_t* loop;
    uv_pipe_t stdin_pipe;
    uv_pipe_t stdout_pipe;
    uv_async_t async_write_handle;
    winpty_t* winpty_instance;

    // Synchronization
    std::mutex input_mutex;
    std::vector<char> input_buffer;
    sigc::connection timeout_conn;

    // Selection state
    bool is_selecting;
    int sel_start_x, sel_start_y;
    int sel_end_x, sel_end_y;
};
