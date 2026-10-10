/* SPDX-License-Identifier: Unlicense */

#pragma once

#include <gtkmm/textbuffer.h>

#include <string>

namespace writeit {

// The buffer's one-format-per-character rule: every character carries at
// most one character format tag (fmt*) and at most one paragraph format tag
// (para*). Two of either conflict, and whichever tag was created later wins
// on screen, in capture() and in the file.
//
// Returns the offset of the first character that breaks the rule, or -1 when
// none does. When `why` is given and a character breaks it, *why names the
// character and its tags, for a test's failure message.
//
// Public for the tests: every suite that edits the real window, and the
// undo fuzz, call it after each action and fail on anything but -1.
int first_doubled_format_tag(const Glib::RefPtr<Gtk::TextBuffer>& buffer,
                             std::string* why = nullptr);

}  // namespace writeit
