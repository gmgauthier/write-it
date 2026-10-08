/* SPDX-License-Identifier: Unlicense */

#include "application.hpp"

#include <glib.h>
#include <glibmm/miscutils.h>

namespace {

bool theme_has_gtk3(const char* name)
{
  const std::string home = Glib::build_filename(Glib::get_home_dir(), ".themes", name, "gtk-3.0");
  const std::string sys = Glib::build_filename("/usr/share/themes", name, "gtk-3.0");
  return g_file_test(home.c_str(), G_FILE_TEST_IS_DIR) ||
         g_file_test(sys.c_str(), G_FILE_TEST_IS_DIR);
}

// Process-only theme. GTK_THEME in the environment still wins.
// Else Clearlooks-Phenix, then Clearlooks, then Adwaita:light.
void prefer_light_theme()
{
  if (g_getenv("GTK_THEME") != nullptr)
    return;
  if (theme_has_gtk3("Clearlooks-Phenix"))
    g_setenv("GTK_THEME", "Clearlooks-Phenix", FALSE);
  else if (theme_has_gtk3("Clearlooks"))
    g_setenv("GTK_THEME", "Clearlooks", FALSE);
  else
    g_setenv("GTK_THEME", "Adwaita:light", FALSE);
}

}  // namespace

int main(int argc, char* argv[])
{
  if (g_getenv("GDK_BACKEND") == nullptr)
    g_setenv("GDK_BACKEND", "x11", FALSE);
  g_set_prgname("write-it");
  prefer_light_theme();
  return writeit::Application::create()->run(argc, argv);
}
