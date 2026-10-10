/* SPDX-License-Identifier: Unlicense */

// Format > Style...'s size field in the real window, under a display. Bug
// Basher's review of #36 found it took anything and converted it silently:
// "abc" and 0 made the style 1 pt, 1639 became 1638, 10.3 became 10.5, 10.2
// became 10 and 10.75 became 11. It refuses as the toolbar's size box does
// (size_refusal()), with the same messages, in a modal "Write-It" message
// box: the dialog stays open, the field keeps the keyboard with its text
// selected to type over, and the style is unchanged. OK by Enter in the
// field and by the OK button both refuse, and so do switching to another
// style and New.... 10.5 and " 012 " are taken.

#include "check.hpp"
#include "font_sizes.hpp"
#include "main_window.hpp"

#include <glib.h>
#include <glib/gstdio.h>
#include <glibmm/miscutils.h>
#include <gtkmm.h>

#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace writeit {

// Test-only access to the window's privates; declared a friend there.
struct MainWindowProbe {
  static void load(MainWindow& w, const Document& doc)
  {
    w.replace_buffer(doc, 0);
  }
  static void place(MainWindow& w, int offset)
  {
    w.buffer_->place_cursor(w.buffer_->get_iter_at_offset(offset));
  }
  static Document capture(MainWindow& w)
  {
    return w.capture();
  }
  static size_t undo_steps(MainWindow& w)
  {
    return w.undo_.size();
  }
  static void style_dialog(MainWindow& w)
  {
    w.on_style_dialog();
  }
};

}  // namespace writeit

namespace {

using writeit::MainWindowProbe;

void settle()
{
  auto context = Glib::MainContext::get_default();
  for (int round = 0; round < 4; ++round) {
    while (context->pending())
      context->iteration(false);
    g_usleep(20000);
  }
  while (context->pending())
    context->iteration(false);
}

writeit::Paragraph para(const std::string& text)
{
  writeit::Paragraph p;
  writeit::Run run;
  run.text = text;
  p.runs.push_back(run);
  return p;
}

// The size of `name` in the window's style sheet, or -1.
double style_size(writeit::MainWindow& window, const std::string& name)
{
  const auto sheet =
      writeit::complete_sheet(writeit::style_sheet(MainWindowProbe::capture(window)));
  const writeit::Style* style = writeit::find_style(sheet, name);
  return style ? style->format.size : -1;
}

// The first widget under `root` that `match` takes, or nullptr.
Gtk::Widget* find_widget(Gtk::Widget* root, const std::function<bool(Gtk::Widget*)>& match)
{
  std::vector<Gtk::Widget*> todo{root};
  while (!todo.empty()) {
    Gtk::Widget* widget = todo.back();
    todo.pop_back();
    if (match(widget))
      return widget;
    if (auto* container = dynamic_cast<Gtk::Container*>(widget))
      for (Gtk::Widget* child : container->get_children())
        todo.push_back(child);
  }
  return nullptr;
}

// The field a label names, by its mnemonic text ("Si_ze:").
Gtk::Widget* labelled(Gtk::Widget* root, const char* text)
{
  auto* label = dynamic_cast<Gtk::Label*>(find_widget(root, [text](Gtk::Widget* widget) {
    auto* label = dynamic_cast<Gtk::Label*>(widget);
    return label && label->get_label() == text;
  }));
  return label ? label->get_mnemonic_widget() : nullptr;
}

Gtk::Dialog* visible_dialog(const char* title)
{
  for (Gtk::Window* top : Gtk::Window::list_toplevels()) {
    auto* dialog = dynamic_cast<Gtk::Dialog*>(top);
    if (dialog && !dynamic_cast<Gtk::MessageDialog*>(top) && dialog->get_visible() &&
        dialog->get_title() == title)
      return dialog;
  }
  return nullptr;
}

bool message_showing()
{
  for (Gtk::Window* top : Gtk::Window::list_toplevels()) {
    auto* message = dynamic_cast<Gtk::MessageDialog*>(top);
    if (message && message->get_visible())
      return true;
  }
  return false;
}

// Answers each message box from inside its run(), and remembers the last:
// what it said, its title, whether it was modal and over the Style dialog.
struct Messages {
  int count = 0;
  std::string last;
  std::string title;
  bool modal = false;
  bool over_style = false;
  sigc::connection poll;
  Messages()
  {
    poll = Glib::signal_timeout().connect(
        [this] {
          for (Gtk::Window* top : Gtk::Window::list_toplevels()) {
            auto* message = dynamic_cast<Gtk::MessageDialog*>(top);
            if (message && message->get_visible()) {
              ++count;
              last = message->property_text().get_value();
              title = message->get_title();
              modal = message->get_modal();
              Gtk::Window* parent = message->get_transient_for();
              over_style = parent && parent->get_title() == "Style";
              message->response(Gtk::RESPONSE_OK);
            }
          }
          return true;
        },
        20);
  }
  ~Messages()
  {
    poll.disconnect();
  }
};

// How the entry is offered: Enter in the field (it activates OK), the OK
// button (focus leaves the field first, as a click moves it), switching the
// Style box to Heading 1, or New....
enum class Press { Enter, Ok, Switch, New };

// What Format > Style... did with a size typed into its size field.
struct Attempt {
  bool found = false;      // the dialog and its size field
  int messages = 0;        // message boxes shown
  bool open = false;       // the dialog still open after them
  bool focus = false;      // the size field has the keyboard
  bool selected = false;   // with all its text selected
  std::string text;        // and that text
  std::string style;       // the style the dialog shows
  bool new_style = false;  // New...'s name dialog opened
};

// Opens Format > Style..., types `text` into the size field and offers it
// as `press` says. Once any message is answered, notes the dialog's state
// and cancels it; a dialog that closed on its own has applied the entry.
Attempt attempt(writeit::MainWindow& window, Messages& messages, const std::string& text,
                Press press)
{
  Attempt got;
  const int seen = messages.count;
  int state = 0;
  int polls = 0;
  auto driver = Glib::signal_timeout().connect(
      [&]() {
        ++polls;
        if (Gtk::Dialog* ask = visible_dialog("New Style")) {
          got.new_style = true;
          ask->response(Gtk::RESPONSE_CANCEL);
          return true;
        }
        Gtk::Dialog* dialog = visible_dialog("Style");
        if (!dialog)
          return polls < 500;
        auto* size = dynamic_cast<Gtk::SpinButton*>(labelled(dialog, "Si_ze:"));
        auto* chooser = dynamic_cast<Gtk::ComboBoxText*>(labelled(dialog, "_Style:"));
        if (!size || !chooser)
          return polls < 500;
        if (state == 0) {
          got.found = true;
          state = 1;
          polls = 0;
          size->grab_focus();
          size->set_text(text);
          // Pressed from its own source: New... runs its own dialog, which
          // this driver, blocked inside it, could not answer.
          Glib::signal_idle().connect_once([dialog, size, chooser, press] {
            switch (press) {
              case Press::Enter:
                size->activate();
                break;
              case Press::Ok:
                if (Gtk::Widget* ok = dialog->get_widget_for_response(Gtk::RESPONSE_OK)) {
                  ok->grab_focus();
                  if (auto* button = dynamic_cast<Gtk::Button*>(ok))
                    button->clicked();
                }
                break;
              case Press::Switch:
                chooser->set_active_text("Heading 1");
                break;
              case Press::New:
                if (auto* make =
                        dynamic_cast<Gtk::Button*>(find_widget(dialog, [](Gtk::Widget* widget) {
                          auto* button = dynamic_cast<Gtk::Button*>(widget);
                          return button && button->get_label() == "_New…";
                        })))
                  make->clicked();
                break;
            }
          });
          return true;
        }
        // Waiting for the message, then for it to be answered.
        if ((messages.count == seen || message_showing()) && polls < 150)
          return true;
        got.open = true;
        got.focus = size->is_focus();
        int start = 0;
        int end = 0;
        const Glib::ustring shown = size->get_text();
        got.text = shown.raw();
        got.selected = size->get_selection_bounds(start, end) && start == 0 &&
                       end == static_cast<int>(shown.length());
        got.style = chooser->get_active_text().raw();
        dialog->response(Gtk::RESPONSE_CANCEL);
        return false;
      },
      20);
  MainWindowProbe::style_dialog(window);
  driver.disconnect();
  settle();
  got.messages = messages.count - seen;
  return got;
}

struct Refused {
  const char* text;
  Press press;
  const char* message;
};

}  // namespace

// Exactly the checks this suite runs. Update it with the tests.
constexpr int kChecks = 73;

int main(int argc, char* argv[])
{
  std::string home = Glib::build_filename(Glib::get_tmp_dir(), "write-it-style-size-XXXXXX");
  if (!g_mkdtemp(&home[0]))
    return EXIT_FAILURE;
  g_setenv("XDG_CONFIG_HOME", home.c_str(), TRUE);
  g_setenv("GDK_BACKEND", "x11", FALSE);
  if (!gtk_init_check(&argc, &argv)) {
    std::cout << "style-size-ui: no display, skipped\n";
    return 77;
  }
  Gtk::Main kit(argc, argv);
  Messages messages;
  // A dialog nobody answers would hang the suite: fail instead.
  Glib::signal_timeout().connect_seconds(
      [] {
        std::cerr << "style-size-ui: stuck in a dialog\n";
        std::exit(EXIT_FAILURE);
        return false;
      },
      50);
  {
    writeit::MainWindow window;
    window.show();
    settle();
    writeit::Document doc = writeit::blank_document("Sans", 11);
    doc.paragraphs = {para("Title"), para("Body text")};
    CHECK(writeit::apply_style(doc, 0, 0, "Heading 1"));
    MainWindowProbe::load(window, doc);
    MainWindowProbe::place(window, 8);  // in "Body text", Normal
    settle();
    CHECK(style_size(window, "Normal") == 11);

    const std::string kNotNumber = "This is not a valid number.";
    const std::string kRange = "The number must be between 1 and 1638.";
    const std::string kHalf = "Font sizes must be whole numbers or end in .5.";
    const std::vector<Refused> refused = {
        {"abc", Press::Enter, kNotNumber.c_str()},  {"0", Press::Ok, kRange.c_str()},
        {"1639", Press::Enter, kRange.c_str()},     {"10.3", Press::Ok, kHalf.c_str()},
        {"10.2", Press::Enter, kHalf.c_str()},      {"10.75", Press::Ok, kHalf.c_str()},
        {"abc", Press::Switch, kNotNumber.c_str()}, {"10.3", Press::New, kHalf.c_str()},
    };
    for (const Refused& entry : refused) {
      const writeit::Document before = MainWindowProbe::capture(window);
      const size_t steps = MainWindowProbe::undo_steps(window);
      const Attempt got = attempt(window, messages, entry.text, entry.press);
      const bool said = got.messages == 1 && messages.last == entry.message;
      if (!said || !got.open || !got.focus || !got.selected)
        std::cerr << "  \"" << entry.text << "\": " << got.messages << " message(s), last \""
                  << messages.last << "\", open " << got.open << ", focus " << got.focus
                  << ", selected " << got.selected << ", text \"" << got.text << "\"\n";
      CHECK(got.found);
      // The toolbar box's own words for the entry, in a modal message
      // over the dialog.
      CHECK(said && writeit::size_refusal(entry.text) == entry.message);
      CHECK(messages.title == "Write-It" && messages.modal && messages.over_style);
      // The dialog stays open, on the same style, with the entry selected
      // in the field, which has the keyboard.
      CHECK(got.open && got.style == "Normal" && !got.new_style);
      CHECK(got.focus && got.selected && got.text == entry.text);
      // The style, and the document, are as they were.
      CHECK(MainWindowProbe::capture(window) == before &&
            MainWindowProbe::undo_steps(window) == steps);
      CHECK(style_size(window, "Normal") == 11);
      // The pure function agrees, so a refusal here is one there.
      CHECK(writeit::parse_size(entry.text) == 0);
    }

    // Sizes the box takes, the dialog takes: 10.5, and " 012 " trimmed with
    // its leading zero ignored. Each closes the dialog and is one undo step.
    {
      const size_t steps = MainWindowProbe::undo_steps(window);
      const int seen = messages.count;
      const Attempt half = attempt(window, messages, "10.5", Press::Enter);
      CHECK(half.found && !half.open && messages.count == seen);
      CHECK(style_size(window, "Normal") == 10.5 &&
            MainWindowProbe::undo_steps(window) == steps + 1);
      const Attempt padded = attempt(window, messages, " 012 ", Press::Ok);
      CHECK(padded.found && !padded.open && messages.count == seen);
      CHECK(style_size(window, "Normal") == 12 && MainWindowProbe::undo_steps(window) == steps + 2);
      // Heading 1 is untouched by both.
      CHECK(style_size(window, "Heading 1") == 16);
    }

    // After a refusal, a good size in the same dialog still applies.
    {
      int state = 0;
      int seen = messages.count;
      auto driver = Glib::signal_timeout().connect(
          [&]() {
            Gtk::Dialog* dialog = visible_dialog("Style");
            auto* size =
                dialog ? dynamic_cast<Gtk::SpinButton*>(labelled(dialog, "Si_ze:")) : nullptr;
            if (!size || message_showing())
              return true;
            if (state == 0) {
              state = 1;
              size->grab_focus();
              size->set_text("1639");
              size->activate();
            } else if (messages.count > seen) {
              size->set_text("14");
              size->activate();
              return false;
            }
            return true;
          },
          20);
      MainWindowProbe::style_dialog(window);
      driver.disconnect();
      settle();
      CHECK(messages.count == seen + 1 && messages.last == kRange);
      CHECK(style_size(window, "Normal") == 14);
    }

    window.hide();
    settle();
  }
  g_remove(Glib::build_filename(home, "write-it", "write-it.ini").c_str());
  g_rmdir(Glib::build_filename(home, "write-it").c_str());
  g_rmdir(home.c_str());
  return suite_test::done("style-size-ui", kChecks);
}
