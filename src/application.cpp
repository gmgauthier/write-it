/* SPDX-License-Identifier: Unlicense */

#include "application.hpp"
#include "config.hpp"
#include "main_window.hpp"
#include "open_plan.hpp"

namespace writeit {

Glib::RefPtr<Application> Application::create()
{
  return Glib::RefPtr<Application>(new Application());
}

Application::Application()
    : Gtk::Application(APP_ID, Gio::APPLICATION_HANDLES_OPEN)
{
}

MainWindow* Application::new_window()
{
  auto* window = new MainWindow();
  add_window(*window);
  window->set_open_elsewhere([this](const std::string& path) {
    std::vector<MainWindow*> windows;
    std::vector<WindowState> states;
    for (auto* each : get_windows()) {
      if (auto* main = dynamic_cast<MainWindow*>(each)) {
        windows.push_back(main);
        states.push_back({main->document_path(), main->pristine(), main->import_source()});
      }
    }
    const int holding = window_holding(states, path);
    if (holding < 0)
      return false;
    windows[static_cast<size_t>(holding)]->present();
    return true;
  });
  // A closed window is finished with; the application keeps no list.
  window->signal_hide().connect([window] { delete window; });
  window->present();
  return window;
}

void Application::on_activate()
{
  new_window();
}

void Application::on_open(const type_vec_files& files, const Glib::ustring&)
{
  std::vector<OpenRequest> requests;
  requests.reserve(files.size());
  for (const auto& file : files)
    requests.emplace_back(file->get_path(), file->get_uri());  // path "" when not local
  // Open from the main loop, not here: an error dialog would otherwise hold
  // a second launch, which waits for this call to return. The hold keeps
  // the program running until then, even with no window yet.
  hold();
  Glib::signal_idle().connect_once([this, requests] {
    open_paths(requests);
    release();
  });
}

void Application::open_paths(const std::vector<OpenRequest>& requests)
{
  std::vector<MainWindow*> windows;
  std::vector<WindowState> states;
  for (auto* window : get_windows()) {
    if (auto* main = dynamic_cast<MainWindow*>(window)) {
      windows.push_back(main);
      states.push_back({main->document_path(), main->pristine(), main->import_source()});
    }
  }
  const int existing = static_cast<int>(windows.size());

  std::vector<MainWindow*> created;
  std::vector<bool> failed;
  const auto exists = [](const std::string& path) {
    return Glib::file_test(path, Glib::FILE_TEST_EXISTS);
  };
  for (const OpenAction& action : plan_open(requests, states, exists)) {
    switch (action.step) {
      case OpenStep::Present:
        windows[static_cast<size_t>(action.window)]->present();
        break;
      case OpenStep::LoadInto: {
        MainWindow* window = windows[static_cast<size_t>(action.window)];
        window->present();
        window->open_file(action.path);
        break;
      }
      case OpenStep::LoadNew: {
        MainWindow* window = new_window();
        created.push_back(window);
        failed.push_back(!window->open_file(action.path));
        break;
      }
      case OpenStep::RefuseNotLocal: {
        MainWindow* window = nullptr;
        if (!created.empty())
          window = created.back();
        else if (!windows.empty())
          window = windows.front();
        if (window == nullptr) {
          window = new_window();
          created.push_back(window);
          failed.push_back(true);
        }
        window->present();
        window->refuse_not_local(action.uri);
        break;
      }
    }
  }

  const std::vector<bool> close = close_after_open(existing, failed);
  for (size_t i = 0; i < created.size(); ++i) {
    if (close[i])
      created[i]->hide();
  }
}

}  // namespace writeit
