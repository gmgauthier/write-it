/* SPDX-License-Identifier: Unlicense */

#include "templates.hpp"

#include "config.hpp"
#include "filename.hpp"

#include <glibmm/fileutils.h>
#include <glibmm/miscutils.h>

#include <algorithm>

namespace writeit {
namespace {

bool rtf_name(const std::string& name)
{
  return extension_of(name) == ".rtf";
}

}  // namespace

std::vector<std::string> shipped_templates()
{
  const std::string source = Glib::build_filename(SOURCE_ROOT, "data", "templates");
  const std::string installed = Glib::build_filename(DATADIR, "templates");
  const std::string dir = Glib::file_test(source, Glib::FILE_TEST_IS_DIR) ? source : installed;
  std::vector<std::string> paths;
  try {
    Glib::Dir listing(dir);
    for (const std::string& name : listing) {
      if (!rtf_name(name))
        continue;
      const std::string path = Glib::build_filename(dir, name);
      if (Glib::file_test(path, Glib::FILE_TEST_IS_REGULAR))
        paths.push_back(path);
    }
  } catch (const Glib::Error&) {
    return {};
  }
  std::sort(paths.begin(), paths.end(), [](const std::string& a, const std::string& b) {
    return Glib::path_get_basename(a) < Glib::path_get_basename(b);
  });
  return paths;
}

}  // namespace writeit
