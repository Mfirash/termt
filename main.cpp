#include <gtkmm/application.h>
#include <gtkmm/window.h>
#include "termwidget.h"

int main(int argc, char* argv[]) {
    auto app = Gtk::Application::create(argc, argv, "");

    Gtk::Window win;
    win.set_title("termt");
    win.set_default_size(800, 450);
    termwidget term;
    win.add(term);
    win.show_all();
    term.fork_shell();
    return app->run(win);
}