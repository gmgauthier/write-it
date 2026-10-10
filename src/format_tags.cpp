/* SPDX-License-Identifier: Unlicense */

#include "format_tags.hpp"

namespace writeit {

int first_doubled_format_tag(const Glib::RefPtr<Gtk::TextBuffer>& buffer, std::string* why)
{
  if (!buffer)
    return -1;
  for (auto iter = buffer->begin(); !iter.is_end(); ++iter) {
    int fmt = 0;
    int para = 0;
    std::string names;
    for (const auto& tag : iter.get_tags()) {
      const std::string name = tag->property_name().get_value();
      const bool is_fmt = name.rfind("fmt", 0) == 0;
      const bool is_para = name.rfind("para", 0) == 0;
      fmt += is_fmt;
      para += is_para;
      if (is_fmt || is_para)
        names += " " + name;
    }
    if (fmt > 1 || para > 1) {
      if (why) {
        const gunichar c = iter.get_char();
        *why = "character " + std::to_string(iter.get_offset()) + " '" +
               (c == '\n' ? std::string("\\n") : Glib::ustring(1, c).raw()) + "' has" + names;
      }
      return iter.get_offset();
    }
  }
  return -1;
}

}  // namespace writeit
