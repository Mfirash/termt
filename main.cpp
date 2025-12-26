#include <gtkmm/application.h>
#include <gtkmm/window.h>
#include "termwidget.h"
#include <cstdlib>
#include <string>

class terme : public Gtk::Window {
public:
    terme(const std::string& command) {
        set_title("termt");
        set_default_size(800, 450);
        add(m_term);
        m_term.show();
        m_term.fork_shell(command);
    }
protected:
    termwidget m_term;
};

int main(int argc, char* argv[]) {
    std::string command = (argc > 1) ? argv[1] : "C:\\Windows\\System32\\cmd.exe";
    auto app = Gtk::Application::create("io.github.mfirash.termt");
    terme window(command);
    return app->run(window, 0, nullptr);
}