/* SPDX-License-Identifier: Unlicense */

#include "application.hpp"
#include "config.hpp"
#include "main_window.hpp"

namespace writeit {

Glib::RefPtr<Application> Application::create()
{
  return Glib::RefPtr<Application>(new Application());
}

Application::Application()
    : Gtk::Application(APP_ID)
{
}

void Application::on_activate()
{
  auto* window = new MainWindow();
  add_window(*window);
  window->present();
}

}  // namespace writeit
