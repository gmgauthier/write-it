/* SPDX-License-Identifier: Unlicense */

// The real window under a display (CI runs the suite in xvfb-run). A window
// narrower than the toolbars moves their last controls into each toolbar's
// overflow menu, the » arrow at its end. Every control that can go there must
// arrive with its name, and choosing it there must do what the control does.
//
// First, at 960 px (the first launch), at the narrowest the window can be
// made, and at 640 px: a toolbar with overflowed items shows its arrow where
// it can be clicked, every overflowed item has a proxy with a label, and the
// arrow's menu lists exactly those, labelled and visible. Then each
// proxy is chosen in turn and checked against what its button or box does.

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

namespace writeit {

// Test-only access to the window's privates; declared a friend there.
struct MainWindowProbe {
  static Gtk::Toolbar& standard(MainWindow& w)
  {
    return w.standard_bar_;
  }
  static Gtk::Toolbar& format(MainWindow& w)
  {
    return w.format_bar_;
  }
  static Glib::RefPtr<Gtk::TextBuffer> buffer(MainWindow& w)
  {
    return w.buffer_;
  }
  static Run format_at(MainWindow& w, int offset)
  {
    return w.format_of(w.buffer_->get_iter_at_offset(offset));
  }
  static ParaFormat para(MainWindow& w)
  {
    return w.para_at(w.cursor_offset());
  }
  static void update_actions(MainWindow& w)
  {
    w.update_actions();
  }
  static bool dirty(MainWindow& w)
  {
    return w.dirty();
  }
  // The tool item that holds each control, by the control's tooltip.
  static Gtk::ToolItem* tool(MainWindow& w, const char* tip)
  {
    for (Gtk::Toolbar* bar : {&w.standard_bar_, &w.format_bar_}) {
      for (int i = 0; i < bar->get_n_items(); ++i) {
        Gtk::ToolItem* item = bar->get_nth_item(i);
        Gtk::Widget* control = item->get_child();
        // A tool button keeps its tooltip on its inner button.
        if (item->get_tooltip_text() == tip || (control && control->get_tooltip_text() == tip))
          return item;
      }
    }
    return nullptr;
  }
  static Gtk::ToggleToolButton* toggle(MainWindow& w, const char* tip)
  {
    return dynamic_cast<Gtk::ToggleToolButton*>(tool(w, tip));
  }
  static Gtk::ComboBoxText& font_combo(MainWindow& w)
  {
    return w.font_combo_;
  }
  static Gtk::ComboBoxText& size_combo(MainWindow& w)
  {
    return w.size_combo_;
  }
  static Gtk::ComboBoxText& style_combo(MainWindow& w)
  {
    return w.style_combo_;
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

// A menu label as shown: the mnemonic underscores dropped.
std::string shown(const Glib::ustring& label)
{
  std::string out;
  for (char c : label.raw())
    if (c != '_')
      out += c;
  return out;
}

std::string label_of(Gtk::Widget* widget)
{
  auto* item = dynamic_cast<Gtk::MenuItem*>(widget);
  return item ? shown(item->get_label()) : std::string();
}

// What a tool item is called, for messages.
std::string name_of(Gtk::ToolItem* item)
{
  if (!item->get_tooltip_text().empty())
    return item->get_tooltip_text();
  if (item->get_child())
    return item->get_child()->get_tooltip_text();
  return "?";
}

// The toolbar's items that did not fit: shown, but not on the bar.
std::vector<Gtk::ToolItem*> overflowed(Gtk::Toolbar& bar)
{
  std::vector<Gtk::ToolItem*> out;
  for (int i = 0; i < bar.get_n_items(); ++i) {
    Gtk::ToolItem* item = bar.get_nth_item(i);
    if (item->get_visible() && !item->get_child_visible() &&
        !dynamic_cast<Gtk::SeparatorToolItem*>(item))
      out.push_back(item);
  }
  return out;
}

// Every overflowed item has a proxy menu item with a label.
bool proxies_labelled(Gtk::Toolbar& bar, const char* where)
{
  bool ok = true;
  for (Gtk::ToolItem* item : overflowed(bar)) {
    Gtk::Widget* proxy = item->retrieve_proxy_menu_item();
    if (!proxy || label_of(proxy).empty()) {
      std::cerr << where << ": " << name_of(item) << " overflows with no labelled proxy\n";
      ok = false;
    }
  }
  return ok;
}

// The toolbar's overflow arrow, an internal child.
Gtk::ToggleButton* arrow_of(Gtk::Toolbar& bar)
{
  Gtk::ToggleButton* arrow = nullptr;
  gtk_container_forall(
      GTK_CONTAINER(bar.gobj()),
      [](GtkWidget* child, gpointer data) {
        if (GTK_IS_TOGGLE_BUTTON(child))
          *static_cast<Gtk::ToggleButton**>(data) = Glib::wrap(GTK_TOGGLE_BUTTON(child));
      },
      &arrow);
  return arrow;
}

// A bar with overflowed items shows its arrow where it can be clicked: laid
// out, with a size, inside the bar.
bool arrow_reachable(Gtk::Toolbar& bar, const char* where)
{
  if (overflowed(bar).empty())
    return true;
  Gtk::ToggleButton* arrow = arrow_of(bar);
  if (!arrow || !arrow->get_visible() || !arrow->get_mapped()) {
    std::cerr << where << ": items overflow but the arrow is not shown\n";
    return false;
  }
  const Gtk::Allocation a = arrow->get_allocation();
  const Gtk::Allocation b = bar.get_allocation();
  const bool ok = a.get_width() > 1 && a.get_height() > 1 && a.get_x() >= b.get_x() &&
                  a.get_x() + a.get_width() <= b.get_x() + b.get_width();
  if (!ok)
    std::cerr << where << ": arrow at " << a.get_x() << " " << a.get_width() << "x"
              << a.get_height() << ", bar " << b.get_x() << " to " << b.get_x() + b.get_width()
              << "\n";
  return ok;
}

// Opens the arrow's menu as a click does and reads it: the label of every
// entry that is not a separator, or "" for an entry with none or not shown.
std::vector<std::string> arrow_menu(Gtk::Toolbar& bar)
{
  std::vector<std::string> labels;
  Gtk::ToggleButton* arrow = arrow_of(bar);
  if (!arrow || !arrow->get_visible())
    return labels;
  arrow->clicked();
  settle();
  GList* menus = gtk_menu_get_for_attach_widget(GTK_WIDGET(bar.gobj()));
  if (menus) {
    auto* menu = Glib::wrap(GTK_MENU(menus->data));
    for (Gtk::Widget* child : menu->get_children()) {
      if (dynamic_cast<Gtk::SeparatorMenuItem*>(child))
        continue;
      labels.push_back(child->get_visible() && child->get_child_visible() ? label_of(child) : "");
    }
    menu->popdown();
  }
  if (arrow->get_active())
    arrow->set_active(false);
  settle();
  return labels;
}

// The arrow's menu lists every overflowed item, each with its label.
bool menu_matches(Gtk::Toolbar& bar, const char* where)
{
  const std::vector<Gtk::ToolItem*> items = overflowed(bar);
  const std::vector<std::string> labels = arrow_menu(bar);
  bool ok = labels.size() == items.size();
  for (const std::string& label : labels)
    ok = ok && !label.empty();
  if (!ok) {
    std::cerr << where << ": " << items.size() << " overflowed, menu shows";
    for (const std::string& label : labels)
      std::cerr << " [" << label << "]";
    std::cerr << "\n";
  }
  return ok;
}

// Resizes the window and lets the toolbars lay out.
void resize(writeit::MainWindow& window, int width)
{
  window.resize(width, 700);
  settle();
}

int minimum_width(writeit::MainWindow& window)
{
  int minimum = 0;
  int natural = 0;
  window.get_preferred_width(minimum, natural);
  return minimum;
}

Gtk::MenuItem* proxy_of(writeit::MainWindow& window, const char* tip)
{
  Gtk::ToolItem* item = MainWindowProbe::tool(window, tip);
  if (!item) {
    std::cerr << "no tool item " << tip << "\n";
    return nullptr;
  }
  auto* proxy = dynamic_cast<Gtk::MenuItem*>(item->retrieve_proxy_menu_item());
  if (!proxy)
    std::cerr << tip << ": no proxy\n";
  return proxy;
}

bool proxy_active(writeit::MainWindow& window, const char* tip)
{
  auto* check = dynamic_cast<Gtk::CheckMenuItem*>(proxy_of(window, tip));
  return check && check->get_active();
}

// Chooses the proxy as the overflow menu does.
void choose(writeit::MainWindow& window, const char* tip)
{
  if (Gtk::MenuItem* proxy = proxy_of(window, tip))
    proxy->activate();
  settle();
}

// The entries of a combo proxy's submenu, by label.
std::vector<Gtk::MenuItem*> entries(writeit::MainWindow& window, const char* tip)
{
  std::vector<Gtk::MenuItem*> out;
  Gtk::MenuItem* proxy = proxy_of(window, tip);
  if (!proxy || !proxy->get_submenu())
    return out;
  for (Gtk::Widget* child : proxy->get_submenu()->get_children())
    if (auto* item = dynamic_cast<Gtk::MenuItem*>(child))
      out.push_back(item);
  return out;
}

Gtk::CheckMenuItem* entry(writeit::MainWindow& window, const char* tip, const std::string& label)
{
  for (Gtk::MenuItem* item : entries(window, tip))
    if (label_of(item) == label)
      return dynamic_cast<Gtk::CheckMenuItem*>(item);
  return nullptr;
}

Glib::ustring text(writeit::MainWindow& window)
{
  auto buffer = MainWindowProbe::buffer(window);
  return buffer->get_text(buffer->begin(), buffer->end());
}

void select(writeit::MainWindow& window, int from, int to)
{
  auto buffer = MainWindowProbe::buffer(window);
  buffer->select_range(buffer->get_iter_at_offset(from), buffer->get_iter_at_offset(to));
  settle();
}

// Answers the next dialog the window runs with `response`, and notes its
// title (or its message, for a message dialog).
struct Responder {
  std::string seen;
  sigc::connection poll;

  Responder(writeit::MainWindow& window, int response)
  {
    poll = Glib::signal_timeout().connect(
        [this, &window, response] {
          for (Gtk::Window* top : Gtk::Window::list_toplevels()) {
            auto* dialog = dynamic_cast<Gtk::Dialog*>(top);
            if (!dialog || !dialog->get_visible() || dialog->get_transient_for() != &window)
              continue;
            seen = dialog->get_title();
            if (auto* message = dynamic_cast<Gtk::MessageDialog*>(dialog))
              seen = message->property_text().get_value();
            dialog->response(response);
            return false;
          }
          return true;
        },
        50);
  }
  ~Responder()
  {
    poll.disconnect();
  }
};

}  // namespace

// Exactly the checks this suite runs, loops included. Update it with the tests.
constexpr int kChecks = 60;

int main(int argc, char* argv[])
{
  std::string home = Glib::build_filename(Glib::get_tmp_dir(), "write-it-overflow-XXXXXX");
  if (!g_mkdtemp(&home[0]))
    return EXIT_FAILURE;
  g_setenv("XDG_CONFIG_HOME", home.c_str(), TRUE);
  g_setenv("GDK_BACKEND", "x11", FALSE);
  if (!gtk_init_check(&argc, &argv)) {
    std::cout << "toolbar-overflow: no display, skipped\n";
    return 77;
  }
  Gtk::Main kit(argc, argv);
  {
    writeit::MainWindow window;
    window.show();
    settle();
    auto& standard = MainWindowProbe::standard(window);
    auto& format = MainWindowProbe::format(window);

    // The first launch, 960 px: the format bar runs out of room.
    int width = 0;
    int height = 0;
    window.get_size(width, height);
    CHECK(width == 960);
    CHECK(!overflowed(format).empty());
    CHECK(proxies_labelled(standard, "960 px, standard"));
    CHECK(proxies_labelled(format, "960 px, format"));
    CHECK(arrow_reachable(standard, "960 px, standard"));
    CHECK(arrow_reachable(format, "960 px, format"));
    CHECK(menu_matches(standard, "960 px, standard"));
    CHECK(menu_matches(format, "960 px, format"));

    // The narrowest the window goes: both bars overflow.
    resize(window, 1);
    window.get_size(width, height);
    if (width != minimum_width(window))
      std::cerr << "narrowest: " << width << ", minimum " << minimum_width(window) << "\n";
    CHECK(width == minimum_width(window));
    CHECK(!overflowed(standard).empty());
    CHECK(!overflowed(format).empty());
    CHECK(proxies_labelled(standard, "minimum, standard"));
    CHECK(proxies_labelled(format, "minimum, format"));
    CHECK(arrow_reachable(standard, "minimum, standard"));
    CHECK(arrow_reachable(format, "minimum, format"));
    CHECK(menu_matches(standard, "minimum, standard"));
    CHECK(menu_matches(format, "minimum, format"));

    // 640 px, where the review found the standard overflow blank.
    resize(window, 640);
    CHECK(!overflowed(standard).empty());
    CHECK(arrow_reachable(standard, "640 px, standard"));
    CHECK(arrow_reachable(format, "640 px, format"));
    CHECK(proxies_labelled(standard, "640 px, standard") &&
          proxies_labelled(format, "640 px, format"));
    CHECK(menu_matches(standard, "640 px, standard"));
    CHECK(menu_matches(format, "640 px, format"));
    resize(window, 960);

    // Character format: the proxy sets the button and the text, and follows
    // the button back.
    auto buffer = MainWindowProbe::buffer(window);
    buffer->begin_user_action();
    buffer->insert_interactive_at_cursor("Hello world", true);
    buffer->end_user_action();
    settle();
    for (const char* tip : {"Bold", "Italic", "Underline"}) {
      auto flag = [&window, tip] {
        const writeit::Run run = MainWindowProbe::format_at(window, 1);
        return std::string(tip) == "Bold" ? run.bold : tip[0] == 'I' ? run.italic : run.underline;
      };
      select(window, 0, 5);
      choose(window, tip);
      Gtk::ToggleToolButton* button = MainWindowProbe::toggle(window, tip);
      if (!(button && button->get_active() && flag() && proxy_active(window, tip)))
        std::cerr << tip << ": the proxy did not set it\n";
      CHECK(button && button->get_active() && flag() && proxy_active(window, tip));
      if (button)
        button->set_active(false);
      settle();
      CHECK(!flag() && !proxy_active(window, tip));
    }

    // Alignment: one of three, as the buttons are.
    struct Way {
      const char* tip;
      writeit::Align align;
    };
    const Way ways[] = {{"Center", writeit::Align::Center},
                        {"Align Right", writeit::Align::Right},
                        {"Align Left", writeit::Align::Left}};
    for (const Way& way : ways) {
      choose(window, way.tip);
      bool one = true;
      for (const Way& other : ways) {
        const bool on = other.tip == way.tip;
        Gtk::ToggleToolButton* button = MainWindowProbe::toggle(window, other.tip);
        one = one && button && button->get_active() == on && proxy_active(window, other.tip) == on;
      }
      if (!one || MainWindowProbe::para(window).align != way.align)
        std::cerr << way.tip << ": alignment did not follow the proxy\n";
      CHECK(one && MainWindowProbe::para(window).align == way.align);
    }
    if (Gtk::ToggleToolButton* center = MainWindowProbe::toggle(window, "Center"))
      center->set_active(true);
    settle();
    CHECK(proxy_active(window, "Center") && !proxy_active(window, "Align Left"));
    choose(window, "Align Left");

    // Lists.
    struct List {
      const char* tip;
      writeit::ListKind kind;
    };
    for (const List& list : {List{"Bullets", writeit::ListKind::Bullet},
                             List{"Numbering", writeit::ListKind::Number}}) {
      choose(window, list.tip);
      Gtk::ToggleToolButton* button = MainWindowProbe::toggle(window, list.tip);
      CHECK(button && button->get_active() && proxy_active(window, list.tip) &&
            MainWindowProbe::para(window).list.kind == list.kind);
      choose(window, list.tip);
      CHECK(button && !button->get_active() && !proxy_active(window, list.tip) &&
            MainWindowProbe::para(window).list.kind == writeit::ListKind::None);
    }

    // The boxes: a submenu of their entries, the current one ticked.
    auto& font = MainWindowProbe::font_combo(window);
    const std::string current = font.get_active_text();
    Gtk::CheckMenuItem* ticked = entry(window, "Font", current);
    CHECK(ticked && ticked->get_active());
    std::string other;
    for (Gtk::MenuItem* item : entries(window, "Font"))
      if (other.empty() && label_of(item) != current)
        other = label_of(item);
    select(window, 0, 5);
    if (Gtk::CheckMenuItem* item = entry(window, "Font", other))
      item->activate();
    settle();
    CHECK(!other.empty() && font.get_active_text() == other &&
          MainWindowProbe::format_at(window, 1).font == other);
    CHECK(entries(window, "Font").size() ==
          static_cast<size_t>(font.get_model()->children().size()));

    auto& size = MainWindowProbe::size_combo(window);
    Gtk::CheckMenuItem* eleven = entry(window, "Size", "11");
    CHECK(eleven && eleven->get_active());
    if (Gtk::CheckMenuItem* item = entry(window, "Size", "14"))
      item->activate();
    settle();
    CHECK(size.get_active_text() == "14" && MainWindowProbe::format_at(window, 1).size == 14);
    CHECK(entries(window, "Size").size() == 10);

    // Style waits for #19, as its box does.
    auto& style = MainWindowProbe::style_combo(window);
    Gtk::MenuItem* style_proxy = proxy_of(window, "Style");
    Gtk::CheckMenuItem* body = entry(window, "Style", "Body text");
    CHECK(style_proxy && style_proxy->get_sensitive() == style.get_sensitive() && body &&
          body->get_active() && entries(window, "Style").size() == 1);

    // Standard: Copy, Cut, Paste, Undo, Redo.
    select(window, 0, 5);
    MainWindowProbe::update_actions(window);
    Gtk::MenuItem* copy = proxy_of(window, "Copy");
    CHECK(copy && copy->get_sensitive());
    choose(window, "Copy");
    CHECK(Gtk::Clipboard::get()->wait_for_text() == "Hello");
    select(window, 5, 11);
    choose(window, "Cut");
    CHECK(text(window) == "Hello" && Gtk::Clipboard::get()->wait_for_text() == " world");
    buffer->place_cursor(buffer->end());
    MainWindowProbe::update_actions(window);
    Gtk::MenuItem* paste = proxy_of(window, "Paste");
    CHECK(paste && paste->get_sensitive());
    choose(window, "Paste");
    CHECK(text(window) == "Hello world");
    Gtk::MenuItem* undo = proxy_of(window, "Undo");
    CHECK(undo && undo->get_sensitive());
    choose(window, "Undo");
    CHECK(text(window) == "Hello");
    Gtk::MenuItem* redo = proxy_of(window, "Redo");
    CHECK(redo && redo->get_sensitive());
    choose(window, "Redo");
    CHECK(text(window) == "Hello world");

    // Print waits for M4, as its button does.
    Gtk::MenuItem* print = proxy_of(window, "Print");
    CHECK(print && !print->get_sensitive() && label_of(print) == "Print…");

    // Save on an untitled document asks where, as the button does.
    {
      Responder cancel(window, Gtk::RESPONSE_CANCEL);
      choose(window, "Save");
      CHECK(cancel.seen == "Save As");
    }
    // Open shows the Open dialog.
    {
      Responder cancel(window, Gtk::RESPONSE_CANCEL);
      choose(window, "Open");
      CHECK(cancel.seen == "Open");
    }
    // New asks about the changes, then clears the page.
    CHECK(MainWindowProbe::dirty(window));
    {
      Responder discard(window, Gtk::RESPONSE_REJECT);
      choose(window, "New");
      CHECK(discard.seen.rfind("Save changes", 0) == 0);
    }
    CHECK(text(window).empty() && !MainWindowProbe::dirty(window));

    // Every proxy, overflowed or not, has its label.
    bool labelled = true;
    for (Gtk::Toolbar* bar : {&standard, &format}) {
      for (int i = 0; i < bar->get_n_items(); ++i) {
        Gtk::ToolItem* item = bar->get_nth_item(i);
        if (dynamic_cast<Gtk::SeparatorToolItem*>(item))
          continue;
        if (label_of(item->retrieve_proxy_menu_item()).empty()) {
          std::cerr << name_of(item) << ": no labelled proxy\n";
          labelled = false;
        }
      }
    }
    CHECK(labelled);

    window.hide();
    settle();
  }
  g_remove(Glib::build_filename(home, "write-it", "write-it.ini").c_str());
  g_rmdir(Glib::build_filename(home, "write-it").c_str());
  g_rmdir(home.c_str());
  return suite_test::done("toolbar-overflow", kChecks);
}
