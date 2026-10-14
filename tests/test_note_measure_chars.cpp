/* SPDX-License-Identifier: Unlicense */

// Note boundaries in the joined text are character offsets. A multi-byte
// letter must not push the next note's start to a byte index.

#include "check.hpp"
#include "document.hpp"

#include <glibmm/ustring.h>

#include <string>
#include <vector>

namespace {

void ascii_matches_bytes()
{
  const std::string text = "a\n\nb";
  const std::vector<int> offsets = writeit::note_break_offsets(text);
  const std::string::size_type blank = text.find("\n\n");
  CHECK(offsets.size() == 2);
  CHECK(offsets[0] == 0);
  CHECK(blank != std::string::npos);
  CHECK(offsets[1] == static_cast<int>(blank + 2));
  CHECK(offsets[1] == static_cast<int>(Glib::ustring(text.substr(0, blank + 2)).length()));
}

void utf8_is_characters()
{
  const std::string text = "é\n\nnext";
  const std::string::size_type blank = text.find("\n\n");
  CHECK(blank != std::string::npos);
  const int byte_after = static_cast<int>(blank + 2);
  const int char_after = static_cast<int>(Glib::ustring(text.substr(0, blank + 2)).length());
  CHECK(char_after == 3);
  CHECK(byte_after == 4);
  const std::vector<int> offsets = writeit::note_break_offsets(text);
  CHECK(offsets.size() == 2);
  CHECK(offsets[0] == 0);
  CHECK(offsets[1] == char_after);
  CHECK(offsets[1] != byte_after);
}

}  // namespace

int main()
{
  ascii_matches_bytes();
  utf8_is_characters();
  return suite_test::done("note-measure-chars", 12);
}
