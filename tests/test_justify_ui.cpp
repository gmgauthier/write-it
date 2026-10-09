/* SPDX-License-Identifier: Unlicense */

// Justify in the real window, under a display (CI runs the suite in
// xvfb-run): the format toolbar's Justify button after Align Right, and
// Format > Justify after Align Right, as in Word 97. Either one justifies the
// caret's paragraph with GTK's JUSTIFY_FILL, the buttons act as one group,
// and the justification survives edits that rebuild the buffer from the
// document (another paragraph's alignment, undo and redo).
//
// The window is driven only through its widgets, found by walking the tree.

#include "check.hpp"
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

namespace {

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

// The format toolbar's toggle buttons, in order.
std::vector<Gtk::ToggleToolButton*> toggles(Gtk::Window& window)
{
  std::vector<Gtk::ToggleToolButton*> found;
  walk(window, [&found](Gtk::Widget& widget) {
    if (auto* button = dynamic_cast<Gtk::ToggleToolButton*>(&widget))
      found.push_back(button);
  });
  return found;
}

// A tool item hands its tooltip to the button inside it.
Glib::ustring tip(Gtk::ToolItem& item)
{
  if (!item.get_tooltip_text().empty())
    return item.get_tooltip_text();
  if (Gtk::Widget* child = item.get_child())
    return child->get_tooltip_text();
  return "";
}

int index_of(const std::vector<Gtk::ToggleToolButton*>& buttons, const char* text)
{
  for (size_t i = 0; i < buttons.size(); ++i) {
    if (tip(*buttons[i]) == text)
      return static_cast<int>(i);
  }
  return -1;
}

std::vector<Gtk::MenuItem*> menu_items(Gtk::Window& window, const char* menu)
{
  std::vector<Gtk::MenuItem*> found;
  walk(window, [&](Gtk::Widget& widget) {
    auto* bar = dynamic_cast<Gtk::MenuBar*>(&widget);
    if (!bar)
      return;
    for (Gtk::Widget* child : bar->get_children()) {
      auto* top = dynamic_cast<Gtk::MenuItem*>(child);
      if (!top || top->get_label() != menu || !top->get_submenu())
        continue;
      for (Gtk::Widget* entry : top->get_submenu()->get_children()) {
        if (auto* item = dynamic_cast<Gtk::MenuItem*>(entry))
          found.push_back(item);
      }
    }
  });
  return found;
}

Gtk::MenuItem* item_named(const std::vector<Gtk::MenuItem*>& items, const char* label,
                          int* at = nullptr)
{
  for (size_t i = 0; i < items.size(); ++i) {
    if (items[i]->get_label() == label) {
      if (at)
        *at = static_cast<int>(i);
      return items[i];
    }
  }
  return nullptr;
}

Gtk::TextView* text_view(Gtk::Window& window)
{
  Gtk::TextView* found = nullptr;
  walk(window, [&found](Gtk::Widget& widget) {
    if (!found)
      found = dynamic_cast<Gtk::TextView*>(&widget);
  });
  return found;
}

// The justification GTK lays out the paragraph at `offset` with: the one set
// by a tag on its first character, or the view's own (left).
Gtk::Justification justification_at(Gtk::TextView& view, int offset)
{
  auto buffer = view.get_buffer();
  Gtk::TextIter iter = buffer->get_iter_at_offset(offset);
  iter.set_line_offset(0);
  Gtk::Justification justification = view.get_justification();
  for (const auto& tag : iter.get_tags()) {
    if (tag->property_justification_set())
      justification = tag->property_justification();
  }
  return justification;
}

void place_caret(Gtk::TextView& view, int offset)
{
  auto buffer = view.get_buffer();
  buffer->place_cursor(buffer->get_iter_at_offset(offset));
  settle();
}

void type(Gtk::TextView& view, const char* text)
{
  auto buffer = view.get_buffer();
  buffer->begin_user_action();
  buffer->insert_at_cursor(text);
  buffer->end_user_action();
  settle();
}

}  // namespace

// Exactly the checks this suite runs, loops included. Update it with the tests.
constexpr int kChecks = 35;

int main(int argc, char* argv[])
{
  std::string home = Glib::build_filename(Glib::get_tmp_dir(), "write-it-justify-XXXXXX");
  if (!g_mkdtemp(&home[0]))
    return EXIT_FAILURE;
  g_setenv("XDG_CONFIG_HOME", home.c_str(), TRUE);
  g_setenv("GDK_BACKEND", "x11", FALSE);
  if (!gtk_init_check(&argc, &argv)) {
    std::cout << "justify-ui: no display, skipped\n";
    return 77;
  }
  Gtk::Main kit(argc, argv);
  {
    writeit::MainWindow window;
    window.show();
    settle();

    // The toolbar: Justify straight after Align Right, its icon (or its word
    // when the theme has none), up at first.
    const auto buttons = toggles(window);
    const int right = index_of(buttons, "Align Right");
    const int justify_at = index_of(buttons, "Justify");
    CHECK(right >= 0);
    CHECK(justify_at == right + 1);
    Gtk::ToggleToolButton* left = right >= 2 ? buttons[right - 2] : nullptr;
    Gtk::ToggleToolButton* centre = right >= 1 ? buttons[right - 1] : nullptr;
    Gtk::ToggleToolButton* justify = justify_at >= 0 ? buttons[justify_at] : nullptr;
    CHECK(left && tip(*left) == "Align Left");
    CHECK(centre && tip(*centre) == "Center");
    if (justify) {
      if (Gtk::IconTheme::get_default()->has_icon("format-justify-fill"))
        CHECK(justify->get_icon_name() == "format-justify-fill");
      else
        CHECK(justify->get_label() == "Justify");
      CHECK(!justify->get_active());
      CHECK(justify->get_sensitive());
    } else {
      CHECK(false);
      CHECK(false);
      CHECK(false);
    }

    // The menu: Format > Justify straight after Align Right.
    const auto format = menu_items(window, "F_ormat");
    int right_item_at = -1;
    int justify_item_at = -1;
    Gtk::MenuItem* left_item = item_named(format, "Align _Left");
    Gtk::MenuItem* right_item = item_named(format, "Align _Right", &right_item_at);
    Gtk::MenuItem* justify_item = item_named(format, "_Justify", &justify_item_at);
    CHECK(left_item && right_item);
    CHECK(justify_item != nullptr);
    CHECK(justify_item_at >= 0 && justify_item_at == right_item_at + 1);
    CHECK(justify_item && justify_item->get_sensitive());

    Gtk::TextView* view = text_view(window);
    CHECK(view != nullptr);
    if (view && left && centre && justify && left_item && justify_item) {
      type(*view, "First paragraph, long enough to wrap when it is justified.\nSecond.");
      const int second = 70;
      place_caret(*view, 3);
      CHECK(justification_at(*view, 3) == Gtk::JUSTIFY_LEFT);

      // The button justifies the caret's paragraph and only that one.
      justify->set_active(true);
      settle();
      CHECK(justification_at(*view, 3) == Gtk::JUSTIFY_FILL);
      CHECK(justification_at(*view, second) == Gtk::JUSTIFY_LEFT);
      CHECK(justify->get_active());
      CHECK(!left->get_active());

      // The buttons follow the caret.
      place_caret(*view, second);
      CHECK(!justify->get_active());
      CHECK(left->get_active());
      place_caret(*view, 3);
      CHECK(justify->get_active());

      // Another paragraph's alignment rebuilds the buffer from the document;
      // the first stays justified.
      place_caret(*view, second);
      centre->set_active(true);
      settle();
      CHECK(justification_at(*view, second) == Gtk::JUSTIFY_CENTER);
      CHECK(justification_at(*view, 3) == Gtk::JUSTIFY_FILL);

      // Typing in it and undoing keeps it, and so does redo.
      place_caret(*view, 3);
      type(*view, "x");
      CHECK(justification_at(*view, 3) == Gtk::JUSTIFY_FILL);
      Gtk::MenuItem* undo = item_named(menu_items(window, "_Edit"), "_Undo");
      Gtk::MenuItem* redo = item_named(menu_items(window, "_Edit"), "_Redo");
      CHECK(undo && redo);
      if (undo && redo) {
        undo->activate();
        settle();
        CHECK(justification_at(*view, 3) == Gtk::JUSTIFY_FILL);
        undo->activate();
        settle();
        CHECK(justification_at(*view, second) == Gtk::JUSTIFY_LEFT);
        CHECK(justification_at(*view, 3) == Gtk::JUSTIFY_FILL);
        redo->activate();
        settle();
        CHECK(justification_at(*view, second) == Gtk::JUSTIFY_CENTER);
        CHECK(justification_at(*view, 3) == Gtk::JUSTIFY_FILL);
      } else {
        CHECK(false);
        CHECK(false);
        CHECK(false);
        CHECK(false);
        CHECK(false);
      }

      // Pressing Justify again goes back to left, as Center and Align Right do.
      place_caret(*view, 3);
      justify->set_active(false);
      settle();
      CHECK(justification_at(*view, 3) == Gtk::JUSTIFY_LEFT);
      CHECK(left->get_active());

      // The menu item justifies, and Align Left takes it off.
      justify_item->activate();
      settle();
      CHECK(justification_at(*view, 3) == Gtk::JUSTIFY_FILL);
      CHECK(justify->get_active());
      left_item->activate();
      settle();
      CHECK(justification_at(*view, 3) == Gtk::JUSTIFY_LEFT);
      CHECK(!justify->get_active());
    }
    // Close without the save question.
    window.hide();
    settle();
  }
  g_remove(Glib::build_filename(home, "write-it", "write-it.ini").c_str());
  g_rmdir(Glib::build_filename(home, "write-it").c_str());
  g_rmdir(home.c_str());
  return suite_test::done("justify-ui", kChecks);
}
