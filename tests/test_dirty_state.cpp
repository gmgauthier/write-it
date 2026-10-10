/* SPDX-License-Identifier: Unlicense */

// The real window, under a display. Whether the document has unsaved
// changes: the title's "*", Close's question, and what Open may load over.
// It follows the undo history's state id (the step at the top of the undo
// stack, see undo.hpp), not a copy of the document compared with the
// saved one:
//
//   - New, Open and Save (or Save As) are clean;
//   - any change after them is dirty;
//   - undo back to the saved state is clean again, redo off it dirty, and
//     nothing else clears it: "a" then Backspace is dirty;
//   - the screen-only list passes (list-shift and list-tab tags) never are.
//
// Save ends a run of typing, Delete or Backspace (Greg's decision): "ab",
// Save, "c" makes "c" a step of its own, dirty, and one Undo lands on the
// saved "ab", clean. A run that joins the step on top still gives it a
// fresh id.

#include "check.hpp"
#include "document.hpp"
#include "main_window.hpp"

#include <gdk/gdk.h>
#include <gdk/gdkkeysyms.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <glibmm/fileutils.h>
#include <glibmm/miscutils.h>
#include <gtkmm.h>

#include <cstdlib>
#include <iostream>
#include <string>

namespace writeit {

// Test-only access to the window's privates; declared a friend there.
struct MainWindowProbe {
  static bool open(MainWindow& w, const std::string& path)
  {
    return w.open_path(path, MainWindow::OpenKind::Rtf);
  }
  static bool save_as(MainWindow& w, const std::string& path)
  {
    // What Save As does once its chooser has the name.
    return w.write_rtf(path);
  }
  static bool save(MainWindow& w)
  {
    return w.save_document();
  }
  static void new_document(MainWindow& w)
  {
    w.new_document(false);
  }
  static bool dirty(MainWindow& w)
  {
    return w.dirty();
  }
  static void undo(MainWindow& w)
  {
    w.undo();
  }
  static void redo(MainWindow& w)
  {
    w.redo();
  }
  static size_t undo_depth(MainWindow& w)
  {
    return w.undo_.size();
  }
  // A pause in typing: the next key starts an undo step of its own.
  static void pause(MainWindow& w)
  {
    w.last_typed_us_ = 0;
  }
  static Gtk::TextView& text(MainWindow& w)
  {
    return w.text_;
  }
  static Glib::ustring all(MainWindow& w)
  {
    return w.buffer_->get_text(w.buffer_->begin(), w.buffer_->end());
  }
  static void select_all(MainWindow& w)
  {
    w.buffer_->select_range(w.buffer_->begin(), w.buffer_->end());
  }
  static void bold(MainWindow& w)
  {
    w.toggle_flag(MainWindow::TextFlag::Bold);
  }
  // Bold set (not toggled) over the selection, as Bold does on text that
  // is not all bold yet.
  static void set_bold(MainWindow& w)
  {
    w.apply_run_edit(
        [](Run& run) { MainWindow::set_text_flag(run, MainWindow::TextFlag::Bold, true); });
  }
  static void select(MainWindow& w, int from, int to)
  {
    w.buffer_->select_range(w.buffer_->get_iter_at_offset(from), w.buffer_->get_iter_at_offset(to));
  }
  static void caret(MainWindow& w, int at)
  {
    select(w, at, at);
  }
  static void copy(MainWindow& w)
  {
    w.buffer_->copy_clipboard(Gtk::Clipboard::get());
  }
  // Edit > Paste.
  static void paste(MainWindow& w)
  {
    w.paste_item_->activate();
  }
  static void style(MainWindow& w, const char* name)
  {
    w.apply_named_style(name);
  }
};

}  // namespace writeit

namespace {

using writeit::MainWindowProbe;

void settle()
{
  auto context = Glib::MainContext::get_default();
  for (int round = 0; round < 6; ++round) {
    while (context->pending())
      context->iteration(false);
    g_usleep(20000);
  }
  while (context->pending())
    context->iteration(false);
}

// Key presses queued on the window, as the keyboard delivers them.
void press(writeit::MainWindow& window, guint keyval)
{
  GdkEvent* event = gdk_event_new(GDK_KEY_PRESS);
  event->key.window = GDK_WINDOW(g_object_ref(window.get_window()->gobj()));
  event->key.send_event = TRUE;
  event->key.time = GDK_CURRENT_TIME;
  event->key.keyval = keyval;
  GdkKeymapKey* keys = nullptr;
  gint n = 0;
  if (gdk_keymap_get_entries_for_keyval(gdk_keymap_get_for_display(gdk_display_get_default()),
                                        keyval, &keys, &n) &&
      n > 0) {
    event->key.hardware_keycode = static_cast<guint16>(keys[0].keycode);
    event->key.group = static_cast<guint8>(keys[0].group);
  }
  g_free(keys);
  GdkSeat* seat = gdk_display_get_default_seat(gdk_display_get_default());
  gdk_event_set_device(event, gdk_seat_get_keyboard(seat));
  gdk_event_put(event);
  gdk_event_free(event);
}

void type(writeit::MainWindow& window, const char* text)
{
  for (const char* c = text; *c; ++c) {
    press(window, static_cast<guint>(*c));
    settle();
  }
}

bool starred(writeit::MainWindow& window)
{
  const std::string title = window.get_title();
  return title.size() >= 2 && title.compare(title.size() - 2, 2, " *") == 0;
}

// dirty() and the title agree, and are `want`.
bool is_dirty(writeit::MainWindow& window, bool want, const char* where)
{
  const bool d = MainWindowProbe::dirty(window);
  const bool t = starred(window);
  if (d != want || t != want)
    std::cerr << "  " << where << ": dirty() " << d << ", title \"" << window.get_title()
              << "\", want " << (want ? "dirty" : "clean") << "\n";
  return d == want && t == want;
}

writeit::Document centred_list()
{
  // Centred items get list-shift tags; a wide label on a left-aligned item
  // gets a list-tab tag. Both are on screen only.
  writeit::Document doc;
  for (int i = 0; i < 3; ++i) {
    writeit::Paragraph p;
    writeit::Run run;
    run.text = "Centred item " + std::to_string(i + 1);
    p.runs.push_back(run);
    p.list.kind = writeit::ListKind::Number;
    p.list.list = 1;
    p.indents = writeit::list_indents(0);
    p.align = writeit::Align::Center;
    doc.paragraphs.push_back(p);
  }
  writeit::Paragraph wide;
  writeit::Run run;
  run.text = "Wide label.";
  wide.runs.push_back(run);
  wide.list.kind = writeit::ListKind::Number;
  wide.list.list = 2;
  wide.list.start = 32767;
  wide.indents = writeit::list_indents(0);
  doc.paragraphs.push_back(wide);
  return doc;
}

constexpr int kChecks = 57;

int run(writeit::MainWindow& window, const std::string& home)
{
  const std::string first = Glib::build_filename(home, "first.rtf");
  const std::string second = Glib::build_filename(home, "second.rtf");
  const std::string third = Glib::build_filename(home, "third.rtf");
  const std::string lists = Glib::build_filename(home, "lists.rtf");

  // A new window is clean.
  CHECK(is_dirty(window, false, "new window"));

  // Bug Basher's case: "ab", Save, "c" typed straight on. Save ends the run
  // of typing (Greg's decision), so "c" is a step of its own and makes the
  // document dirty; one Undo lands on the saved "ab", clean.
  type(window, "ab");
  CHECK(is_dirty(window, true, "typed ab"));
  CHECK(MainWindowProbe::save_as(window, first));
  CHECK(is_dirty(window, false, "saved ab"));
  const size_t ab_depth = MainWindowProbe::undo_depth(window);
  type(window, "c");
  CHECK(MainWindowProbe::undo_depth(window) == ab_depth + 1);
  CHECK(is_dirty(window, true, "ab, save, c"));
  MainWindowProbe::undo(window);
  CHECK(MainWindowProbe::all(window) == "ab");
  CHECK(is_dirty(window, false, "undo of c: the save point"));
  MainWindowProbe::redo(window);
  CHECK(MainWindowProbe::all(window) == "abc");
  CHECK(is_dirty(window, true, "redo of c"));
  const size_t depth = MainWindowProbe::undo_depth(window);

  // Saved, then a pause: "d" is its own step. Undo is the save point, clean;
  // redo off it is dirty; undo is clean again.
  CHECK(MainWindowProbe::save(window));
  CHECK(is_dirty(window, false, "saved abc"));
  MainWindowProbe::pause(window);
  type(window, "d");
  CHECK(MainWindowProbe::undo_depth(window) == depth + 1);
  CHECK(is_dirty(window, true, "abc, save, pause, d"));
  MainWindowProbe::undo(window);
  CHECK(MainWindowProbe::all(window) == "abc");
  CHECK(is_dirty(window, false, "undo to the save point"));
  MainWindowProbe::redo(window);
  CHECK(is_dirty(window, true, "redo off the save point"));
  MainWindowProbe::undo(window);
  CHECK(is_dirty(window, false, "undo to the save point again"));
  // Undo past the save point is dirty.
  MainWindowProbe::undo(window);
  CHECK(is_dirty(window, true, "undo past the save point"));
  MainWindowProbe::redo(window);
  CHECK(is_dirty(window, false, "redo back to the save point"));

  // The "*" follows the history, not the text: a key and a Backspace that
  // takes it back leave the text as saved, and dirty.
  MainWindowProbe::pause(window);
  type(window, "x");
  press(window, GDK_KEY_BackSpace);
  settle();
  CHECK(MainWindowProbe::all(window) == "abc");
  CHECK(is_dirty(window, true, "x then Backspace"));
  // Undo all the way back to the save point is clean.
  while (MainWindowProbe::undo_depth(window) > depth)
    MainWindowProbe::undo(window);
  CHECK(MainWindowProbe::all(window) == "abc");
  CHECK(is_dirty(window, false, "undo of x and Backspace"));

  // Formatting is a change, and its undo is clean again.
  MainWindowProbe::pause(window);
  MainWindowProbe::select_all(window);
  MainWindowProbe::bold(window);
  settle();
  CHECK(is_dirty(window, true, "bold"));
  MainWindowProbe::undo(window);
  CHECK(is_dirty(window, false, "undo bold"));
  // So is a named style, one undo step of its own.
  MainWindowProbe::style(window, "Heading 1");
  settle();
  CHECK(is_dirty(window, true, "Heading 1"));
  MainWindowProbe::undo(window);
  CHECK(is_dirty(window, false, "undo Heading 1"));

  // Save As another name: clean, under the new name.
  type(window, "e");
  CHECK(is_dirty(window, true, "typed e"));
  CHECK(MainWindowProbe::save_as(window, second));
  CHECK(is_dirty(window, false, "save as") && window.get_title() == "Write-It - second.rtf");

  // New: clean, and the first key makes it dirty; undo of it clean again.
  MainWindowProbe::new_document(window);
  settle();
  CHECK(is_dirty(window, false, "new"));
  MainWindowProbe::pause(window);
  type(window, "f");
  CHECK(is_dirty(window, true, "typed in new"));
  MainWindowProbe::undo(window);
  CHECK(is_dirty(window, false, "undo in new"));

  // An edit is an edit even when the text comes out the same: typing "c"
  // over a selected "c", or pasting "c" over it, after a save. Each is
  // dirty and one undo step, and Undo takes exactly it away.
  MainWindowProbe::new_document(window);
  settle();
  MainWindowProbe::pause(window);
  type(window, "abc");
  CHECK(MainWindowProbe::save_as(window, third));
  CHECK(is_dirty(window, false, "saved abc again"));
  const size_t saved_depth = MainWindowProbe::undo_depth(window);
  MainWindowProbe::pause(window);
  MainWindowProbe::select(window, 2, 3);
  settle();
  type(window, "c");
  CHECK(MainWindowProbe::all(window) == "abc");
  CHECK(is_dirty(window, true, "c typed over c"));
  CHECK(MainWindowProbe::undo_depth(window) == saved_depth + 1);
  MainWindowProbe::undo(window);
  CHECK(MainWindowProbe::all(window) == "abc");
  CHECK(is_dirty(window, false, "undo of c over c"));
  CHECK(MainWindowProbe::undo_depth(window) == saved_depth);
  MainWindowProbe::pause(window);
  MainWindowProbe::select(window, 2, 3);
  MainWindowProbe::copy(window);
  settle();
  MainWindowProbe::paste(window);
  settle();
  CHECK(MainWindowProbe::all(window) == "abc");
  CHECK(is_dirty(window, true, "c pasted over c"));
  CHECK(MainWindowProbe::undo_depth(window) == saved_depth + 1);
  MainWindowProbe::undo(window);
  CHECK(MainWindowProbe::all(window) == "abc");
  CHECK(is_dirty(window, false, "undo of the paste"));
  CHECK(MainWindowProbe::undo_depth(window) == saved_depth);

  // What never inserts, deletes or changes a format is no edit: no step,
  // and the "*" as it was. Backspace at the very start, Delete at the very
  // end, Bold on text already bold, a style already applied.
  MainWindowProbe::pause(window);
  MainWindowProbe::select(window, 0, 3);
  MainWindowProbe::bold(window);
  settle();
  CHECK(MainWindowProbe::save(window));
  const size_t bold_depth = MainWindowProbe::undo_depth(window);
  auto unchanged = [&](const char* what) {
    settle();
    const bool same =
        MainWindowProbe::all(window) == "abc" && MainWindowProbe::undo_depth(window) == bold_depth;
    if (!same)
      std::cerr << "  " << what << ": undo depth " << MainWindowProbe::undo_depth(window)
                << ", was " << bold_depth << "\n";
    return same && is_dirty(window, false, what);
  };
  MainWindowProbe::caret(window, 0);
  press(window, GDK_KEY_BackSpace);
  CHECK(unchanged("Backspace at the start"));
  MainWindowProbe::caret(window, 3);
  press(window, GDK_KEY_Delete);
  CHECK(unchanged("Delete at the end"));
  MainWindowProbe::select(window, 0, 3);
  MainWindowProbe::set_bold(window);
  CHECK(unchanged("Bold on bold text"));
  MainWindowProbe::style(window, "Normal");
  CHECK(unchanged("Normal on Normal"));

  // Open: clean, though its list passes tag the text on screen once it is
  // in; an edit and its undo as above.
  Glib::file_set_contents(lists, writeit::rtf_export(centred_list()));
  MainWindowProbe::new_document(window);
  CHECK(MainWindowProbe::open(window, lists));
  settle();
  CHECK(is_dirty(window, false, "opened lists"));
  MainWindowProbe::pause(window);
  type(window, "g");
  CHECK(is_dirty(window, true, "typed in opened"));
  MainWindowProbe::undo(window);
  settle();
  CHECK(is_dirty(window, false, "undo in opened"));

  g_remove(first.c_str());
  g_remove(second.c_str());
  g_remove(third.c_str());
  g_remove(lists.c_str());
  return suite_test::done("dirty-state", kChecks);
}

}  // namespace

int main(int argc, char* argv[])
{
  std::string home = Glib::build_filename(Glib::get_tmp_dir(), "write-it-dirty-state-XXXXXX");
  if (!g_mkdtemp(&home[0]))
    return EXIT_FAILURE;
  g_setenv("XDG_CONFIG_HOME", home.c_str(), TRUE);
  g_setenv("GDK_BACKEND", "x11", FALSE);
  if (!gtk_init_check(&argc, &argv)) {
    std::cout << "dirty-state: no display, skipped\n";
    return 77;
  }
  Gtk::Main kit(argc, argv);
  int result = EXIT_FAILURE;
  {
    writeit::MainWindow window;
    window.show();
    settle();
    MainWindowProbe::text(window).grab_focus();
    settle();
    result = run(window, home);
    window.hide();
    settle();
  }
  const std::string ini = Glib::build_filename(home, "write-it", "write-it.ini");
  g_remove(ini.c_str());
  g_rmdir(Glib::build_filename(home, "write-it").c_str());
  g_rmdir(home.c_str());
  return result;
}
