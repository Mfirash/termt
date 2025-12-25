#pragma once
#include <gtkmm.h>
#include <uv.h>
#include <vector>
#include <mutex>
#include "terminalgridmanager.h"

#ifdef _WIN32
#include <winpty.h>
#endif

class termwidget : public Gtk::DrawingArea {
public:
    termwidget();
    virtual ~termwidget();
    void fork_shell();

protected:
    bool on_draw(const Cairo::RefPtr<Cairo::Context>& cr) override;
    bool on_key_press_event(GdkEventKey* event) override;
    void on_size_allocate(Gtk::Allocation& allocation) override; // Dynamic Resizing

private:
    static void on_uv_read(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf);
    static void alloc_buffer(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf);
    static void on_async_write(uv_async_t* handle);
    bool on_timeout();

    uv_loop_t* loop;
    uv_pipe_t stdin_pipe;
    uv_pipe_t stdout_pipe;
    
#ifdef _WIN32
    winpty_t* winpty_instance;
#endif

    uv_async_t async_write_handle;  
    std::vector<char> input_buffer; 
    std::mutex input_mutex;         
    sigc::connection timeout_conn;
};