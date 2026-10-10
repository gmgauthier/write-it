/* SPDX-License-Identifier: Unlicense */

// Format > Paragraph in the real window, under a display (CI runs the suite
// in xvfb-run). Bug Basher's live pass found the indent fields snapping back
// from 99, 30 or 1e3 with no word, and Left 22" with Right 22" accepted and
// the paragraph gone off the page. As in Word 97, OK on either now shows a
// warning and goes back to the dialog with the field to fix selected and
// what was typed still in it; nothing is applied until the choice fits.
//
// The dialogs run their own loops, so the script below answers them from a
// timeout, one step at a time, the way a user would.

#include "check.hpp"
#include "document.hpp"
#include "main_window.hpp"
#include "units.hpp"

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
  static void paragraph(MainWindow& w)
  {
    w.on_paragraph();
  }
  static Indents indents(MainWindow& w)
  {
    return w.indents_at(w.cursor_offset());
  }
  static void units(MainWindow& w, Units units)
  {
    w.settings_.units = units;
  }
  static void zoom(MainWindow& w, int percent)
  {
    w.set_zoom(percent);
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
    g_usleep(10000);
  }
  while (context->pending())
    context->iteration(false);
}

void walk(Gtk::Widget& widget, const std::function<void(Gtk::Widget&)>& visit)
{
  visit(widget);
  if (auto* container = dynamic_cast<Gtk::Container*>(&widget)) {
    for (Gtk::Widget* child : container->get_children())
      walk(*child, visit);
  }
}

// The Paragraph dialog, when it is up over the window.
Gtk::Dialog* paragraph_dialog(Gtk::Window& window)
{
  for (Gtk::Window* top : Gtk::Window::list_toplevels()) {
    auto* dialog = dynamic_cast<Gtk::Dialog*>(top);
    if (dialog && !dynamic_cast<Gtk::MessageDialog*>(dialog) && dialog->get_visible() &&
        dialog->get_transient_for() == &window && dialog->get_title() == "Paragraph")
      return dialog;
  }
  return nullptr;
}

// A message up over `parent`.
Gtk::MessageDialog* message_over(Gtk::Window& parent)
{
  for (Gtk::Window* top : Gtk::Window::list_toplevels()) {
    auto* message = dynamic_cast<Gtk::MessageDialog*>(top);
    if (message && message->get_visible() && message->get_transient_for() == &parent)
      return message;
  }
  return nullptr;
}

// The Paragraph dialog's widgets: Left, Right, By, and Special.
struct Fields {
  Gtk::SpinButton* left = nullptr;
  Gtk::SpinButton* right = nullptr;
  Gtk::SpinButton* by = nullptr;
  Gtk::ComboBoxText* special = nullptr;
};

// Each by the label that names it, as a user finds them.
Fields fields_of(Gtk::Dialog& dialog)
{
  Fields fields;
  walk(dialog, [&](Gtk::Widget& widget) {
    auto* label = dynamic_cast<Gtk::Label*>(&widget);
    if (!label)
      return;
    Gtk::Widget* target = label->get_mnemonic_widget();
    const Glib::ustring text = label->get_label();
    if (text == "_Left:")
      fields.left = dynamic_cast<Gtk::SpinButton*>(target);
    else if (text == "_Right:")
      fields.right = dynamic_cast<Gtk::SpinButton*>(target);
    else if (text == "B_y:")
      fields.by = dynamic_cast<Gtk::SpinButton*>(target);
    else if (text == "_Special:")
      fields.special = dynamic_cast<Gtk::ComboBoxText*>(target);
  });
  return fields;
}

// The field has the dialog's focus and all of its text selected.
bool selected(Gtk::Dialog& dialog, Gtk::SpinButton* field)
{
  int start = -1;
  int end = -1;
  if (!field || dialog.get_focus() != field || !field->get_selection_bounds(start, end))
    return false;
  return start == 0 && end == static_cast<int>(field->get_text().size());
}

// Steps run in order from a timeout while the dialogs are up. A step returns
// false to be tried again later, when what it waits for is not there yet. A
// step that waits too long fails the suite and every dialog is cancelled, so
// a broken dialog cannot hang the test.
struct Script {
  writeit::MainWindow& window;
  std::vector<std::function<bool()>> steps;
  size_t next = 0;
  gint64 since = 0;
  bool stuck = false;
  sigc::connection poll;

  explicit Script(writeit::MainWindow& w)
      : window(w)
  {
  }
  void start()
  {
    since = g_get_monotonic_time();
    poll = Glib::signal_timeout().connect(
        [this] {
          if (stuck || next >= steps.size()) {
            Gtk::Dialog* dialog = paragraph_dialog(window);
            Gtk::MessageDialog* message = dialog ? message_over(*dialog) : nullptr;
            if (message)
              message->response(Gtk::RESPONSE_OK);
            else if (dialog && stuck)
              dialog->response(Gtk::RESPONSE_CANCEL);
            return true;
          }
          if (steps[next]()) {
            ++next;
            since = g_get_monotonic_time();
          } else if (g_get_monotonic_time() - since > 20 * G_USEC_PER_SEC) {
            std::cerr << "paragraph-dialog: step " << next << " never happened\n";
            CHECK(false);
            stuck = true;
          }
          return true;
        },
        30);
  }
  ~Script()
  {
    poll.disconnect();
  }
};

const char* kRangeIn = "The measurement must be between 0\" and 22\".";
const char* kInvalidIn =
    "This is not a valid measurement. The measurement must be between 0\" and 22\".";
const char* kSidesIn =
    "The left and right indents leave too little room for text. They must leave at least "
    "0.25\" of the 6.98\" text area.";
const char* kFirstIn =
    "The left, first-line and right indents leave too little room for text. They must leave "
    "at least 0.25\" of the 6.98\" text area.";
const char* kRangeCm = "The measurement must be between 0 cm and 55.88 cm.";

}  // namespace

// Exactly the checks this suite runs, loops included. Update it with the tests.
constexpr int kChecks = 46;

int main(int argc, char* argv[])
{
  std::string home = Glib::build_filename(Glib::get_tmp_dir(), "write-it-paragraph-XXXXXX");
  if (!g_mkdtemp(&home[0]))
    return EXIT_FAILURE;
  g_setenv("XDG_CONFIG_HOME", home.c_str(), TRUE);
  g_setenv("GDK_BACKEND", "x11", FALSE);
  if (!gtk_init_check(&argc, &argv)) {
    std::cout << "paragraph-dialog: no display, skipped\n";
    return 77;
  }
  Gtk::Main kit(argc, argv);
  {
    writeit::MainWindow window;
    window.show();
    settle();
    MainWindowProbe::units(window, writeit::Units::Inches);

    // Inches. Each warning comes up over the dialog with its text, and when
    // it is answered the dialog is still up, the field to fix is selected,
    // and what was typed is still in it.
    {
      Script script(window);
      Gtk::Dialog* dialog = nullptr;
      Fields f;
      // OK answers a message, then checks the dialog it goes back to.
      auto expect = [&](const char* text) {
        return [&, text] {
          Gtk::MessageDialog* message = dialog ? message_over(*dialog) : nullptr;
          if (!message)
            return false;
          CHECK(message->property_text().get_value() == text);
          CHECK(message->property_message_type().get_value() == Gtk::MESSAGE_WARNING);
          message->response(Gtk::RESPONSE_OK);
          return true;
        };
      };
      auto back = [&](Gtk::SpinButton** field, const char* typed,
                      const std::function<void()>& then) {
        return [&, field, typed, then] {
          if (!dialog || message_over(*dialog))
            return false;
          CHECK(paragraph_dialog(window) == dialog);
          CHECK(*field && (*field)->get_text() == typed);
          CHECK(selected(*dialog, *field));
          then();
          return true;
        };
      };
      script.steps = {
          [&] {
            dialog = paragraph_dialog(window);
            if (!dialog)
              return false;
            f = fields_of(*dialog);
            CHECK(f.left && f.right && f.by && f.special);
            if (!f.left || !f.right || !f.by || !f.special) {
              dialog->response(Gtk::RESPONSE_CANCEL);
              return true;
            }
            // Defect 4: 99" is past the 22" ceiling.
            f.left->set_text("99");
            dialog->response(Gtk::RESPONSE_OK);
            return true;
          },
          expect(kRangeIn),
          back(&f.left, "99",
               [&] {
                 f.left->set_text("30");
                 dialog->response(Gtk::RESPONSE_OK);
               }),
          expect(kRangeIn),
          back(&f.left, "30",
               [&] {
                 // Not a number: the field takes no exponents.
                 f.left->set_text("1e3");
                 dialog->response(Gtk::RESPONSE_OK);
               }),
          expect(kInvalidIn),
          back(&f.left, "1e3",
               [&] {
                 // Defect 3: Left 22" and Right 22".
                 f.left->set_text("22");
                 f.right->set_text("22");
                 dialog->response(Gtk::RESPONSE_OK);
               }),
          expect(kSidesIn),
          // Left alone leaves no room, so Left is the one to fix.
          back(&f.left, "22",
               [&] {
                 // 3" + 3" fits, but not with a 1" first line on top.
                 f.left->set_text("3");
                 f.right->set_text("3");
                 f.special->set_active(1);
                 f.by->set_text("1");
                 dialog->response(Gtk::RESPONSE_OK);
               }),
          expect(kFirstIn),
          back(&f.by, "1",
               [&] {
                 f.by->set_text("0.5");
                 dialog->response(Gtk::RESPONSE_OK);
               }),
      };
      script.start();
      MainWindowProbe::paragraph(window);
      CHECK(script.next == script.steps.size() && !script.stuck);
    }
    settle();
    CHECK(paragraph_dialog(window) == nullptr);
    // Only the choice that fits was applied.
    CHECK(MainWindowProbe::indents(window) == (writeit::Indents{4320, 4320, 720}));

    // Centimetres: the message names the range in centimetres, and Cancel
    // after it leaves the paragraph as it was.
    MainWindowProbe::units(window, writeit::Units::Centimetres);
    {
      Script script(window);
      Gtk::Dialog* dialog = nullptr;
      Fields f;
      script.steps = {
          [&] {
            dialog = paragraph_dialog(window);
            if (!dialog)
              return false;
            f = fields_of(*dialog);
            CHECK(f.right != nullptr);
            if (!f.right) {
              dialog->response(Gtk::RESPONSE_CANCEL);
              return true;
            }
            f.right->set_text("99");
            dialog->response(Gtk::RESPONSE_OK);
            return true;
          },
          [&] {
            Gtk::MessageDialog* message = message_over(*dialog);
            if (!message)
              return false;
            CHECK(message->property_text().get_value() == kRangeCm);
            message->response(Gtk::RESPONSE_OK);
            return true;
          },
          [&] {
            if (message_over(*dialog))
              return false;
            CHECK(f.right->get_text() == "99");
            CHECK(selected(*dialog, f.right));
            dialog->response(Gtk::RESPONSE_CANCEL);
            return true;
          },
      };
      script.start();
      MainWindowProbe::paragraph(window);
      CHECK(script.next == script.steps.size() && !script.stuck);
    }
    settle();
    CHECK(paragraph_dialog(window) == nullptr);
    CHECK(MainWindowProbe::indents(window) == (writeit::Indents{4320, 4320, 720}));

    // Bug Basher, second pass: at 50%, 3.48" + 3.49" and 6.98" + 0 were
    // accepted and the line ran off the page. Both now leave too little room
    // for text: Right is selected for the first, Left (too wide alone) for
    // the second. Cancel leaves the paragraph as it was.
    MainWindowProbe::units(window, writeit::Units::Inches);
    MainWindowProbe::zoom(window, 50);
    settle();
    {
      Script script(window);
      Gtk::Dialog* dialog = nullptr;
      Fields f;
      script.steps = {
          [&] {
            dialog = paragraph_dialog(window);
            if (!dialog)
              return false;
            f = fields_of(*dialog);
            CHECK(f.left && f.right && f.special);
            if (!f.left || !f.right || !f.special) {
              dialog->response(Gtk::RESPONSE_CANCEL);
              return true;
            }
            // No first line, so only Left and Right take the room.
            f.special->set_active(0);
            f.left->set_text("3.48");
            f.right->set_text("3.49");
            dialog->response(Gtk::RESPONSE_OK);
            return true;
          },
          [&] {
            Gtk::MessageDialog* message = message_over(*dialog);
            if (!message)
              return false;
            CHECK(message->property_text().get_value() == kSidesIn);
            message->response(Gtk::RESPONSE_OK);
            return true;
          },
          [&] {
            if (message_over(*dialog))
              return false;
            CHECK(f.right->get_text() == "3.49");
            CHECK(selected(*dialog, f.right));
            f.left->set_text("6.98");
            f.right->set_text("0");
            dialog->response(Gtk::RESPONSE_OK);
            return true;
          },
          [&] {
            Gtk::MessageDialog* message = message_over(*dialog);
            if (!message)
              return false;
            CHECK(message->property_text().get_value() == kSidesIn);
            message->response(Gtk::RESPONSE_OK);
            return true;
          },
          [&] {
            if (message_over(*dialog))
              return false;
            CHECK(f.left->get_text() == "6.98");
            CHECK(selected(*dialog, f.left));
            dialog->response(Gtk::RESPONSE_CANCEL);
            return true;
          },
      };
      script.start();
      MainWindowProbe::paragraph(window);
      CHECK(script.next == script.steps.size() && !script.stuck);
    }
    settle();
    CHECK(paragraph_dialog(window) == nullptr);
    CHECK(MainWindowProbe::indents(window) == (writeit::Indents{4320, 4320, 720}));
  }
  return suite_test::done("paragraph-dialog", kChecks);
}
