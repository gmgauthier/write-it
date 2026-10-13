/* SPDX-License-Identifier: Unlicense */

// Page commands in the real window, under a display (CI runs this in
// xvfb-run). Menus that open a dialog are not activated: a modal dialog
// would stall the suite. Page Break, Insert Row, and Footnote do not.

#include "check.hpp"
#include "document.hpp"
#include "main_window.hpp"

#include <glib.h>
#include <glib/gstdio.h>
#include <glibmm/miscutils.h>
#include <gtkmm.h>

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace writeit {

struct MainWindowProbe {
  static void load(MainWindow& window, const Document& doc)
  {
    window.undo_.clear();
    window.replace_buffer(doc, 0);
    window.saved_state_ = window.undo_.state_id();
    window.save_point_ = true;
    window.last_typed_us_ = 0;
    window.update_title();
    window.update_actions();
    window.sync_format_controls();
  }

  static Document doc(MainWindow& window)
  {
    return window.capture();
  }

  static std::string page(MainWindow& window)
  {
    return window.page_label_.get_text();
  }

  static int seams(MainWindow& window)
  {
    int count = 0;
    auto iter = window.buffer_->begin();
    while (true) {
      for (const auto& tag : iter.get_tags()) {
        if (page_seam_pixels(tag->property_name().get_value()) > 0)
          ++count;
      }
      bool more = false;
      while (!iter.is_end()) {
        if (iter.get_char() == '\n') {
          iter.forward_char();
          more = !iter.is_end();
          break;
        }
        iter.forward_char();
      }
      if (!more)
        break;
    }
    return count;
  }

  static void draft(MainWindow& window, bool on)
  {
    (on ? window.draft_item_ : window.page_item_)->set_active(true);
  }

  static void undo(MainWindow& window)
  {
    window.undo();
  }

  static void caret(MainWindow& window, int offset)
  {
    auto buffer = window.buffer_;
    const int count = buffer->get_char_count();
    const int at = offset < 0 ? 0 : offset > count ? count : offset;
    buffer->place_cursor(buffer->get_iter_at_offset(at));
  }

  static void focus_table_cell(MainWindow& window, int which, int row, int column)
  {
    Gtk::TextView* view = window.nth_cell_view(which, row, column);
    if (!view || !view->get_buffer())
      return;
    view->grab_focus();
    auto buffer = view->get_buffer();
    buffer->place_cursor(buffer->get_char_count() > 0 ? buffer->end() : buffer->begin());
  }

  // The Table menu focuses the body without a click in it. The edited cell
  // stays recorded; only a click or a key in the body drops it.
  static void focus_body_quietly(MainWindow& window)
  {
    window.text_.grab_focus();
  }

  static int cursor(MainWindow& window)
  {
    return window.cursor_offset();
  }

  static bool cell_focused(MainWindow& window, int which, int row, int column)
  {
    Gtk::TextView* view = window.nth_cell_view(which, row, column);
    return view && view->has_focus();
  }

  static double scroll_value(MainWindow& window)
  {
    auto v = window.paste_.get_vadjustment();
    return v ? v->get_value() : 0.0;
  }

  // The user has scrolled to a later page. The wheel does this too: stop
  // following the body insert, which is still on the first line.
  static void scroll_to_end(MainWindow& window)
  {
    auto v = window.paste_.get_vadjustment();
    if (!v)
      return;
    const double page = v->get_page_size();
    const double room = std::max(v->get_lower(), v->get_upper() - std::max(0.0, page));
    v->set_value(room);
    window.follow_caret_ = false;
  }

  // The line that holds a table is as tall as the sheet, and the paragraph
  // after that line starts below the sheet.
  static bool table_line_holds_sheet(MainWindow& window)
  {
    auto buffer = window.buffer_;
    if (!buffer)
      return false;
    for (auto iter = buffer->begin(); !iter.is_end(); ++iter) {
      auto anchor = iter.get_child_anchor();
      if (!anchor)
        continue;
      const std::vector<Gtk::Widget*> widgets = anchor->get_widgets();
      if (widgets.empty() || widgets.front() == nullptr)
        return false;
      int top = 0;
      int height = 0;
      window.text_.get_line_yrange(iter, top, height);
      const int sheet = widgets.front()->get_allocated_height();
      if (sheet <= 1)
        return false;
      const int gap = sheet > height ? sheet - height : height - sheet;
      if (gap > 1)
        return false;
      auto next = iter;
      next.forward_char();
      if (!next.is_end())
        next.forward_char();
      if (next.is_end())
        return true;
      int next_top = 0;
      int next_height = 0;
      window.text_.get_line_yrange(next, next_top, next_height);
      return next_top + 1 >= top + sheet;
    }
    return false;
  }

  static bool marker_raised(MainWindow& window)
  {
    if (!window.buffer_)
      return false;
    for (auto iter = window.buffer_->begin(); !iter.is_end(); ++iter) {
      for (const auto& tag : iter.get_tags()) {
        if (tag->property_rise().get_value() > 0)
          return true;
      }
    }
    return false;
  }

  static bool notes_focused(MainWindow& window)
  {
    return window.notes_view_.has_focus();
  }

  static bool note_label_is(MainWindow& window, const char* text)
  {
    return window.note_label_.get_text() == text;
  }

  static std::string note_number(MainWindow& window, int index)
  {
    if (index < 0 || index >= static_cast<int>(window.note_numbers_.size()))
      return {};
    Gtk::Label* label = window.note_numbers_[static_cast<size_t>(index)];
    return label ? label->get_text().raw() : std::string();
  }

  static int note_number_y(MainWindow& window, int index)
  {
    if (index < 0 || index >= static_cast<int>(window.note_numbers_.size()))
      return -1;
    Gtk::Label* label = window.note_numbers_[static_cast<size_t>(index)];
    if (!label)
      return -1;
    int x = 0;
    int y = 0;
    if (!label->translate_coordinates(window.note_box_, 0, 0, x, y))
      return -1;
    return y;
  }

  static int note_text_y(MainWindow& window, int offset)
  {
    auto buffer = window.notes_view_.get_buffer();
    if (!buffer)
      return -1;
    const int chars = buffer->get_char_count();
    const int at = offset < 0 ? 0 : offset > chars ? chars : offset;
    Gdk::Rectangle loc;
    window.notes_view_.get_iter_location(buffer->get_iter_at_offset(at), loc);
    int wx = 0;
    int wy = 0;
    window.notes_view_.buffer_to_window_coords(Gtk::TEXT_WINDOW_WIDGET, loc.get_x(), loc.get_y(), wx,
                                               wy);
    int x = 0;
    int y = 0;
    if (!window.notes_view_.translate_coordinates(window.note_box_, wx, wy, x, y))
      return -1;
    return y;
  }

  // The first character whose line is below offset 0, or -1 when the note
  // does not wrap.
  static int note_wrap_offset(MainWindow& window)
  {
    auto buffer = window.notes_view_.get_buffer();
    if (!buffer)
      return -1;
    const int top = note_text_y(window, 0);
    if (top < 0)
      return -1;
    const int chars = buffer->get_char_count();
    for (int at = 1; at < chars; ++at) {
      const int y = note_text_y(window, at);
      if (y > top + 4)
        return at;
    }
    return -1;
  }

  static void set_notes(MainWindow& window, const std::string& text)
  {
    if (auto buffer = window.notes_view_.get_buffer())
      buffer->set_text(text);
    window.on_notes_end();
  }

  // One character, as a keystroke in the notes. Returns the smallest
  // pasteboard scroll reached while that key was committed.
  static double type_note(MainWindow& window, const std::string& text)
  {
    auto v = window.paste_.get_vadjustment();
    double low = v ? v->get_value() : 0.0;
    sigc::connection watch;
    if (v) {
      watch = v->signal_value_changed().connect([&] {
        low = std::min(low, v->get_value());
      });
    }
    auto buffer = window.notes_view_.get_buffer();
    window.notes_view_.grab_focus();
    if (buffer) {
      buffer->place_cursor(buffer->end());
      buffer->begin_user_action();
      buffer->insert_at_cursor(text);
      buffer->end_user_action();
    }
    watch.disconnect();
    return low;
  }

  // Scroll, note band, and body length while `text` is typed into the notes.
  // The watches stay up through the idles that follow the key, which is when
  // the page used to jump.
  struct NoteMotion {
    double low_scroll = 0;
    double end_scroll = 0;
    int low_height = 0;
    int high_y = 0;
    int low_body = 0;
    // The band grew and then got shorter, or moved down the page. A new line
    // only grows the band and moves it up.
    bool dipped = false;
  };

  static int note_band_y(MainWindow& window)
  {
    return window.note_y_;
  }

  static int note_band_height(MainWindow& window)
  {
    return window.note_box_.get_allocated_height();
  }

  static NoteMotion type_note_motion(MainWindow& window, const std::string& text)
  {
    NoteMotion motion;
    auto v = window.paste_.get_vadjustment();
    motion.low_scroll = v ? v->get_value() : 0;
    motion.low_height = window.note_box_.get_allocated_height();
    motion.high_y = window.note_y_;
    motion.low_body = window.buffer_ ? window.buffer_->get_char_count() : 0;
    int peak_h = motion.low_height;
    int best_y = motion.high_y;
    sigc::connection scroll_watch;
    sigc::connection size_watch;
    sigc::connection body_watch;
    if (v) {
      scroll_watch = v->signal_value_changed().connect([&] {
        motion.low_scroll = std::min(motion.low_scroll, v->get_value());
      });
    }
    size_watch = window.note_box_.signal_size_allocate().connect([&](Gtk::Allocation& alloc) {
      const int h = alloc.get_height();
      const int y = window.note_y_;
      if (h + 4 < peak_h || y > best_y + 4)
        motion.dipped = true;
      peak_h = std::max(peak_h, h);
      best_y = std::min(best_y, y);
      motion.low_height = std::min(motion.low_height, h);
      motion.high_y = std::max(motion.high_y, y);
    });
    if (window.buffer_) {
      body_watch = window.buffer_->signal_changed().connect([&] {
        motion.low_body = std::min(motion.low_body, window.buffer_->get_char_count());
      });
    }
    window.notes_view_.grab_focus();
    auto buffer = window.notes_view_.get_buffer();
    auto context = Glib::MainContext::get_default();
    auto pump = [&] {
      for (int round = 0; round < 6; ++round) {
        while (context->pending())
          context->iteration(false);
      }
    };
    if (buffer) {
      buffer->place_cursor(buffer->end());
      buffer->begin_user_action();
      buffer->insert_at_cursor(text);
      buffer->end_user_action();
      // A real key commits, then scrolls that view's caret on screen. The
      // insert above is the commit. The scroll is what the key does next.
      gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(window.notes_view_.gobj()),
                                         gtk_text_buffer_get_insert(buffer->gobj()));
    }
    pump();
    scroll_watch.disconnect();
    size_watch.disconnect();
    body_watch.disconnect();
    motion.end_scroll = v ? v->get_value() : 0;
    return motion;
  }

  // The note column is on the first sheet: below the marker, and in the
  // lower part of that sheet rather than under the line or past the page.
  static bool note_on_first_sheet(MainWindow& window)
  {
    if (!window.note_box_.get_visible() || !window.buffer_)
      return false;
    const int note_h = window.note_box_.get_allocated_height();
    const int sheet = window.page_.get_allocated_height();
    if (note_h <= 1 || sheet <= 1)
      return false;
    int marker_y = -1;
    for (auto iter = window.buffer_->begin(); !iter.is_end(); ++iter) {
      bool raised = false;
      for (const auto& tag : iter.get_tags()) {
        if (tag->property_rise().get_value() > 0)
          raised = true;
      }
      if (!raised)
        continue;
      Gdk::Rectangle loc;
      window.text_.get_iter_location(iter, loc);
      int wx = 0;
      int wy = 0;
      window.text_.buffer_to_window_coords(Gtk::TEXT_WINDOW_WIDGET, loc.get_x(), loc.get_y(), wx,
                                            wy);
      int px = 0;
      if (!window.text_.translate_coordinates(window.page_, wx, wy, px, marker_y))
        return false;
      break;
    }
    if (marker_y < 0)
      return false;
    // GtkOverlay keeps the child's allocation at 0,0 and moves a GdkWindow.
    // The window origin is where the note is actually drawn.
    auto note_win = window.note_box_.get_window();
    auto page_win = window.page_.get_window();
    if (!note_win || !page_win)
      return false;
    int note_root_x = 0;
    int note_root_y = 0;
    int page_root_x = 0;
    int page_root_y = 0;
    note_win->get_origin(note_root_x, note_root_y);
    page_win->get_origin(page_root_x, page_root_y);
    const int ny = note_root_y - page_root_y;
    const int bottom = ny + std::max(note_h, note_win->get_height());
    return ny > marker_y && ny > sheet * 3 / 5 && bottom <= sheet;
  }

  static bool first_pixbuf(MainWindow& window, int& width, int& height)
  {
    width = 0;
    height = 0;
    if (!window.buffer_)
      return false;
    for (auto iter = window.buffer_->begin(); !iter.is_end(); ++iter) {
      auto pix = iter.get_pixbuf();
      if (!pix)
        continue;
      width = pix->get_width();
      height = pix->get_height();
      return true;
    }
    return false;
  }

  // The same step Insert → Picture takes after the file is read. The menu
  // itself opens a dialog, which this suite does not run.
  static void commit(MainWindow& window, const Document& before, const Document& after, int caret)
  {
    window.commit_document(before, after, caret);
  }

  // Insert → Picture after the file is read. The menu opens a dialog, which
  // this suite does not run.
  static void insert_picture(MainWindow& window, const std::string& bytes)
  {
    Image image;
    image.type = "png";
    image.data = bytes;
    window.insert_picture(std::move(image));
  }

  static void place_cell_caret(MainWindow& window, int which, int row, int column, int offset)
  {
    Gtk::TextView* view = window.nth_cell_view(which, row, column);
    if (!view || !view->get_buffer())
      return;
    auto buffer = view->get_buffer();
    const int count = buffer->get_char_count();
    const int at = offset < 0 ? 0 : offset > count ? count : offset;
    buffer->place_cursor(buffer->get_iter_at_offset(at));
  }

  static int cell_caret(MainWindow& window, int which, int row, int column)
  {
    Gtk::TextView* view = window.nth_cell_view(which, row, column);
    if (!view || !view->get_buffer())
      return -1;
    return view->get_buffer()->get_insert()->get_iter().get_offset();
  }

  static void select_cell(MainWindow& window, int which, int row, int column)
  {
    Gtk::TextView* view = window.nth_cell_view(which, row, column);
    if (!view || !view->get_buffer())
      return;
    auto buffer = view->get_buffer();
    buffer->select_range(buffer->begin(), buffer->end());
  }

  static bool context_cut_on(MainWindow& window)
  {
    return window.context_cut_ && window.context_cut_->get_sensitive();
  }

  static bool context_copy_on(MainWindow& window)
  {
    return window.context_copy_ && window.context_copy_->get_sensitive();
  }

  // Overlay y of a body footnote marker, in the same space as the note band.
  static int marker_overlay_y(MainWindow& window, int note)
  {
    if (!window.buffer_ || note <= 0)
      return -1;
    for (auto iter = window.buffer_->begin(); !iter.is_end(); ++iter) {
      int found = 0;
      for (const auto& tag : iter.get_tags()) {
        const std::string name = tag->property_name().get_value().raw();
        if (name.compare(0, 4, "fmt\x1f") != 0)
          continue;
        int fields = 1;
        size_t last = 0;
        for (size_t i = 0; i < name.size(); ++i) {
          if (name[i] != '\x1f')
            continue;
          ++fields;
          last = i + 1;
        }
        if (fields != 8)
          continue;
        int n = 0;
        bool digits = last < name.size();
        for (size_t i = last; i < name.size(); ++i) {
          if (name[i] < '0' || name[i] > '9') {
            digits = false;
            break;
          }
          n = n * 10 + (name[i] - '0');
        }
        if (digits && n == note)
          found = n;
      }
      if (found == 0)
        continue;
      int top = 0;
      int height = 0;
      window.text_.get_line_yrange(iter, top, height);
      int wx = 0;
      int wy = 0;
      window.text_.buffer_to_window_coords(Gtk::TEXT_WINDOW_WIDGET, 0, top, wx, wy);
      int ox = 0;
      int oy = 0;
      if (!window.text_.translate_coordinates(window.page_overlay_, wx, wy, ox, oy))
        return -1;
      return oy;
    }
    return -1;
  }

  // The band that shows this note: its overlay y, its text, and its number.
  static bool note_band_at(MainWindow& window, int note, int& y, std::string& text,
                           std::string& number)
  {
    y = -1;
    text.clear();
    number.clear();
    if (note <= 0)
      return false;
    const int index = note - 1;
    Gtk::TextView* view = &window.notes_view_;
    y = window.note_y_;
    if (window.note_groups_.size() > 1) {
      bool found = false;
      for (size_t g = 0; g < window.note_groups_.size(); ++g) {
        const auto& indexes = window.note_groups_[g].indexes;
        if (std::find(indexes.begin(), indexes.end(), index) == indexes.end())
          continue;
        found = true;
        if (g == 0) {
          view = &window.notes_view_;
          y = window.note_y_;
        } else if (g - 1 < window.note_bands_.size() && window.note_bands_[g - 1]) {
          view = &window.note_bands_[g - 1]->view;
          y = window.note_bands_[g - 1]->y;
        }
        break;
      }
      if (!found)
        return false;
    }
    if (!view || !view->get_buffer())
      return false;
    text = view->get_buffer()->get_text().raw();
    if (index >= 0 && index < static_cast<int>(window.note_numbers_.size()) &&
        window.note_groups_.size() <= 1) {
      Gtk::Label* label = window.note_numbers_[static_cast<size_t>(index)];
      if (label)
        number = label->get_text().raw();
    } else if (window.note_groups_.size() > 1) {
      for (size_t g = 0; g < window.note_groups_.size(); ++g) {
        const auto& indexes = window.note_groups_[g].indexes;
        auto at = std::find(indexes.begin(), indexes.end(), index);
        if (at == indexes.end())
          continue;
        const size_t slot = static_cast<size_t>(at - indexes.begin());
        const std::vector<Gtk::Label*>& labels =
            g == 0 ? window.note_numbers_
                   : (g - 1 < window.note_bands_.size() && window.note_bands_[g - 1]
                          ? window.note_bands_[g - 1]->numbers
                          : window.note_numbers_);
        if (slot < labels.size() && labels[slot])
          number = labels[slot]->get_text().raw();
      }
    }
    return y >= 0;
  }

  static void type_in_note(MainWindow& window, int note, const std::string& text)
  {
    Gtk::TextView* view = window.view_for_note(note);
    if (!view || !view->get_buffer())
      return;
    view->grab_focus();
    auto buffer = view->get_buffer();
    buffer->place_cursor(buffer->end());
    buffer->begin_user_action();
    buffer->insert_at_cursor(text);
    buffer->end_user_action();
  }
};

}  // namespace writeit

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

Gtk::MenuItem* item_named(const std::vector<Gtk::MenuItem*>& items, const char* label)
{
  for (Gtk::MenuItem* item : items) {
    if (item->get_label() == label)
      return item;
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

void activate(Gtk::MenuItem* item)
{
  if (item)
    item->activate();
  settle();
}

Gtk::ToolButton* tool_named(Gtk::Window& window, const char* tip)
{
  Gtk::ToolButton* found = nullptr;
  walk(window, [&](Gtk::Widget& widget) {
    if (found)
      return;
    auto* button = dynamic_cast<Gtk::ToolButton*>(&widget);
    if (!button)
      return;
    // A tool button keeps its tooltip on its inner button.
    Gtk::Widget* child = button->get_child();
    if (button->get_tooltip_text() == tip || (child && child->get_tooltip_text() == tip))
      found = button;
  });
  return found;
}

// One left click in a cell's text window, through GTK's event path.
void click_view(Gtk::TextView& view, double x, double y, guint time)
{
  auto text_win = view.get_window(Gtk::TEXT_WINDOW_TEXT);
  if (!text_win)
    return;
  int origin_x = 0;
  int origin_y = 0;
  text_win->get_origin(origin_x, origin_y);
  auto pointer = text_win->get_display()->get_default_seat()->get_pointer();
  auto send = [&](GdkEventType type) {
    GdkEvent* event = gdk_event_new(type);
    event->button.window = GDK_WINDOW(g_object_ref(text_win->gobj()));
    event->button.send_event = TRUE;
    event->button.time = time;
    event->button.x = x;
    event->button.y = y;
    event->button.x_root = origin_x + x;
    event->button.y_root = origin_y + y;
    event->button.button = 1;
    event->button.state = type == GDK_BUTTON_RELEASE ? GDK_BUTTON1_MASK : 0;
    if (pointer) {
      event->button.device = pointer->gobj();
      gdk_event_set_source_device(event, pointer->gobj());
    }
    gtk_main_do_event(event);
    gdk_event_free(event);
  };
  send(GDK_BUTTON_PRESS);
  send(GDK_BUTTON_RELEASE);
  settle();
}

// Press near the right edge, move left, and release. `dx` is in window pixels.
void drag_view(Gtk::TextView& view, double x, double y, double dx, guint time)
{
  auto text_win = view.get_window(Gtk::TEXT_WINDOW_TEXT);
  if (!text_win)
    return;
  int origin_x = 0;
  int origin_y = 0;
  text_win->get_origin(origin_x, origin_y);
  auto pointer = text_win->get_display()->get_default_seat()->get_pointer();
  auto device = [&](GdkEvent* event) {
    if (!pointer)
      return;
    gdk_event_set_device(event, pointer->gobj());
    gdk_event_set_source_device(event, pointer->gobj());
  };
  GdkEvent* press = gdk_event_new(GDK_BUTTON_PRESS);
  press->button.window = GDK_WINDOW(g_object_ref(text_win->gobj()));
  press->button.send_event = TRUE;
  press->button.time = time;
  press->button.x = x;
  press->button.y = y;
  press->button.x_root = origin_x + x;
  press->button.y_root = origin_y + y;
  press->button.button = 1;
  device(press);
  gtk_main_do_event(press);
  gdk_event_free(press);

  GdkEvent* motion = gdk_event_new(GDK_MOTION_NOTIFY);
  motion->motion.window = GDK_WINDOW(g_object_ref(text_win->gobj()));
  motion->motion.send_event = TRUE;
  motion->motion.time = time + 1;
  motion->motion.x = x + dx;
  motion->motion.y = y;
  motion->motion.x_root = origin_x + x + dx;
  motion->motion.y_root = origin_y + y;
  motion->motion.state = GDK_BUTTON1_MASK;
  device(motion);
  gtk_main_do_event(motion);
  gdk_event_free(motion);

  GdkEvent* release = gdk_event_new(GDK_BUTTON_RELEASE);
  release->button.window = GDK_WINDOW(g_object_ref(text_win->gobj()));
  release->button.send_event = TRUE;
  release->button.time = time + 2;
  release->button.x = x + dx;
  release->button.y = y;
  release->button.x_root = origin_x + x + dx;
  release->button.y_root = origin_y + y;
  release->button.button = 1;
  release->button.state = GDK_BUTTON1_MASK;
  device(release);
  gtk_main_do_event(release);
  gdk_event_free(release);
  settle();
}

std::string cell_text(const writeit::Document& doc, int row, int column)
{
  std::string out;
  for (const writeit::Paragraph& paragraph : doc.paragraphs) {
    if (paragraph.cell.table == 0 || paragraph.cell.row != row || paragraph.cell.column != column)
      continue;
    for (const writeit::Run& run : paragraph.runs)
      out += run.text;
  }
  return out;
}

// Text of one cell, with "\x01" where `bytes` is the picture.
std::string cell_marks(const writeit::Document& doc, int row, int column, const std::string& bytes)
{
  std::string out;
  for (const writeit::Paragraph& paragraph : doc.paragraphs) {
    if (paragraph.cell.table == 0 || paragraph.cell.row != row || paragraph.cell.column != column)
      continue;
    for (const writeit::Run& run : paragraph.runs) {
      if (run.image && run.image->data == bytes)
        out.push_back('\x01');
      else
        out += run.text;
    }
  }
  return out;
}

bool body_has_image(const writeit::Document& doc)
{
  for (const writeit::Paragraph& paragraph : doc.paragraphs) {
    if (paragraph.cell.table != 0)
      continue;
    for (const writeit::Run& run : paragraph.runs) {
      if (run.image)
        return true;
    }
  }
  return false;
}

// Body paragraphs only, one line each. A paste at the table anchor shows up here.
std::string body_text(const writeit::Document& doc)
{
  std::string out;
  for (const writeit::Paragraph& paragraph : doc.paragraphs) {
    if (paragraph.cell.table != 0)
      continue;
    for (const writeit::Run& run : paragraph.runs)
      out += run.text;
    out.push_back('\n');
  }
  return out;
}

// Body offset of the first table. A table is one anchor character.
int table_anchor(const writeit::Document& doc)
{
  int offset = 0;
  for (const writeit::Paragraph& paragraph : doc.paragraphs) {
    if (paragraph.cell.table != 0)
      return offset;
    int chars = 0;
    for (const writeit::Run& run : paragraph.runs)
      chars += static_cast<int>(Glib::ustring(run.text).length());
    offset += chars + 1;
  }
  return offset;
}

}  // namespace

// Exactly the checks this suite runs. Update it with the tests.
constexpr int kChecks = 191;

int main(int argc, char* argv[])
{
  std::string home = Glib::build_filename(Glib::get_tmp_dir(), "write-it-page-XXXXXX");
  if (!g_mkdtemp(&home[0]))
    return EXIT_FAILURE;
  g_setenv("XDG_CONFIG_HOME", home.c_str(), TRUE);
  g_setenv("GDK_BACKEND", "x11", FALSE);
  if (!gtk_init_check(&argc, &argv)) {
    std::cout << "page-ui: no display, skipped\n";
    return 77;
  }
  Gtk::Main kit(argc, argv);
  {
    writeit::MainWindow window;
    window.show();
    settle();

    const auto file = menu_items(window, "_File");
    const auto format = menu_items(window, "F_ormat");
    const auto insert = menu_items(window, "_Insert");
    const auto table = menu_items(window, "T_able");
    const auto view = menu_items(window, "_View");
    Gtk::MenuItem* page_setup = item_named(file, "Page Set_up…");
    Gtk::MenuItem* columns = item_named(format, "C_olumns…");
    Gtk::MenuItem* picture = item_named(insert, "_Picture…");
    Gtk::MenuItem* table_insert = item_named(insert, "_Table…");
    Gtk::MenuItem* page_break = item_named(insert, "Page _Break");
    Gtk::MenuItem* footnote = item_named(insert, "_Footnote");
    Gtk::MenuItem* insert_table = item_named(table, "_Insert Table…");
    Gtk::MenuItem* insert_row = item_named(table, "Insert _Row");
    Gtk::MenuItem* insert_column = item_named(table, "Insert _Column");
    Gtk::MenuItem* delete_row = item_named(table, "_Delete Row");
    Gtk::MenuItem* delete_column = item_named(table, "Delete C_olumn");
    Gtk::MenuItem* table_width = item_named(table, "Table _Width…");
    Gtk::MenuItem* header = item_named(view, "_Header and Footer");
    CHECK(page_setup && page_setup->get_sensitive());
    CHECK(columns && columns->get_sensitive());
    CHECK(picture && picture->get_sensitive());
    CHECK(table_insert && table_insert->get_sensitive());
    CHECK(page_break && page_break->get_sensitive());
    CHECK(footnote && footnote->get_sensitive());
    CHECK(insert_table && insert_table->get_sensitive());
    CHECK(insert_row && !insert_row->get_sensitive());
    CHECK(insert_column && !insert_column->get_sensitive());
    CHECK(delete_row && !delete_row->get_sensitive());
    CHECK(delete_column && !delete_column->get_sensitive());
    CHECK(table_width && !table_width->get_sensitive());
    auto* header_check = dynamic_cast<Gtk::CheckMenuItem*>(header);
    CHECK(header_check && !header_check->get_active());

    Gtk::TextView* body = text_view(window);
    CHECK(body != nullptr);
    CHECK(body && body->get_buffer()->get_char_count() == 0);

    if (body && page_break && footnote && insert_row && delete_row) {
      body->get_buffer()->begin_user_action();
      body->get_buffer()->insert_at_cursor("Hello");
      body->get_buffer()->end_user_action();
      settle();
      CHECK(body->get_buffer()->get_text() == "Hello");

      activate(page_break);
      writeit::Document broken = writeit::MainWindowProbe::doc(window);
      CHECK(broken.paragraphs.size() == 2);
      CHECK(!broken.paragraphs[0].runs.empty() && broken.paragraphs[0].runs[0].text == "Hello");
      CHECK(!broken.paragraphs[0].page_break);
      CHECK(broken.paragraphs[1].page_break);
      CHECK(broken.paragraphs[1].runs.empty());
      CHECK(writeit::MainWindowProbe::page(window) == "Page 2 of 2");
      CHECK(writeit::MainWindowProbe::seams(window) >= 1);
      body->get_buffer()->begin_user_action();
      body->get_buffer()->insert_at_cursor("A");
      body->get_buffer()->end_user_action();
      settle();
      writeit::Document typed = writeit::MainWindowProbe::doc(window);
      CHECK(typed.paragraphs.size() == 2 && typed.paragraphs[1].page_break &&
            !typed.paragraphs[1].runs.empty() && typed.paragraphs[1].runs[0].text == "A");
      CHECK(writeit::MainWindowProbe::page(window) == "Page 2 of 2");
      writeit::MainWindowProbe::undo(window);
      settle();
      writeit::Document emptied = writeit::MainWindowProbe::doc(window);
      CHECK(emptied.paragraphs.size() == 2 && emptied.paragraphs[1].page_break &&
            emptied.paragraphs[1].runs.empty());
      writeit::MainWindowProbe::draft(window, true);
      settle();
      CHECK(writeit::MainWindowProbe::page(window) == "Page 2 of 2");
      CHECK(writeit::MainWindowProbe::seams(window) == 0);
      writeit::MainWindowProbe::draft(window, false);
      settle();
      CHECK(writeit::MainWindowProbe::page(window) == "Page 2 of 2");
      writeit::MainWindowProbe::undo(window);
      settle();
      writeit::Document restored = writeit::MainWindowProbe::doc(window);
      CHECK(restored.paragraphs.size() == 1);
      CHECK(!restored.paragraphs[0].page_break);
      CHECK(writeit::MainWindowProbe::page(window) == "Page 1 of 1");

      writeit::MainWindowProbe::caret(window, 0);
      activate(page_break);
      writeit::Document leading = writeit::MainWindowProbe::doc(window);
      CHECK(leading.paragraphs.size() == 1 && leading.paragraphs[0].page_break);
      CHECK(!leading.paragraphs[0].runs.empty() && leading.paragraphs[0].runs[0].text == "Hello");
      CHECK(writeit::MainWindowProbe::page(window) == "Page 2 of 2");
      writeit::MainWindowProbe::undo(window);
      settle();

      writeit::MainWindowProbe::caret(window, 2);
      activate(page_break);
      writeit::Document split = writeit::MainWindowProbe::doc(window);
      CHECK(split.paragraphs.size() == 2);
      CHECK(!split.paragraphs[0].runs.empty() && split.paragraphs[0].runs[0].text == "He");
      CHECK(split.paragraphs[1].page_break);
      CHECK(!split.paragraphs[1].runs.empty() && split.paragraphs[1].runs[0].text == "llo");
      CHECK(writeit::MainWindowProbe::page(window) == "Page 2 of 2");
      writeit::MainWindowProbe::undo(window);
      settle();
      writeit::Document joined = writeit::MainWindowProbe::doc(window);
      CHECK(joined.paragraphs.size() == 1);
      CHECK(!joined.paragraphs[0].runs.empty() && joined.paragraphs[0].runs[0].text == "Hello");

      // Enter between the two halves of a page-break paragraph. The break
      // stays on the first half. Backspace joins them, and Enter again still
      // leaves the second half on that sheet.
      writeit::MainWindowProbe::caret(window, 0);
      activate(page_break);
      writeit::MainWindowProbe::caret(window, 2);
      body->get_buffer()->begin_user_action();
      body->get_buffer()->insert_at_cursor("\n");
      body->get_buffer()->end_user_action();
      settle();
      writeit::Document entered = writeit::MainWindowProbe::doc(window);
      CHECK(entered.paragraphs.size() == 2);
      CHECK(entered.paragraphs[0].page_break);
      CHECK(!entered.paragraphs[0].runs.empty() && entered.paragraphs[0].runs[0].text == "He");
      CHECK(!entered.paragraphs[1].page_break);
      CHECK(!entered.paragraphs[1].runs.empty() && entered.paragraphs[1].runs[0].text == "llo");
      CHECK(writeit::MainWindowProbe::page(window) == "Page 2 of 2");
      {
        auto buffer = body->get_buffer();
        auto iter = buffer->get_insert()->get_iter();
        auto prev = iter;
        prev.backward_char();
        buffer->begin_user_action();
        buffer->erase(prev, iter);
        buffer->end_user_action();
      }
      settle();
      writeit::Document rejoined = writeit::MainWindowProbe::doc(window);
      CHECK(rejoined.paragraphs.size() == 1 && rejoined.paragraphs[0].page_break);
      CHECK(writeit::MainWindowProbe::page(window) == "Page 2 of 2");
      body->get_buffer()->begin_user_action();
      body->get_buffer()->insert_at_cursor("\n");
      body->get_buffer()->end_user_action();
      settle();
      writeit::Document again = writeit::MainWindowProbe::doc(window);
      CHECK(again.paragraphs.size() == 2 && again.paragraphs[0].page_break &&
            !again.paragraphs[1].page_break);
      CHECK(writeit::MainWindowProbe::page(window) == "Page 2 of 2");
      writeit::MainWindowProbe::undo(window);
      settle();
      writeit::MainWindowProbe::undo(window);
      settle();
      writeit::MainWindowProbe::undo(window);
      settle();
      writeit::MainWindowProbe::caret(window, 0);
      body->get_buffer()->begin_user_action();
      body->get_buffer()->insert_at_cursor("\n");
      body->get_buffer()->end_user_action();
      settle();
      writeit::Document above = writeit::MainWindowProbe::doc(window);
      CHECK(above.paragraphs.size() == 2 && !above.paragraphs[0].page_break &&
            above.paragraphs[0].runs.empty() && above.paragraphs[1].page_break);
      CHECK(writeit::MainWindowProbe::page(window) == "Page 2 of 2");
      writeit::MainWindowProbe::undo(window);
      settle();
      writeit::MainWindowProbe::caret(window, 5);
      body->get_buffer()->begin_user_action();
      body->get_buffer()->insert_at_cursor("\n");
      body->get_buffer()->end_user_action();
      settle();
      writeit::Document below = writeit::MainWindowProbe::doc(window);
      CHECK(below.paragraphs.size() == 2 && below.paragraphs[0].page_break &&
            !below.paragraphs[0].runs.empty() && below.paragraphs[0].runs[0].text == "Hello" &&
            !below.paragraphs[1].page_break && below.paragraphs[1].runs.empty());
      CHECK(writeit::MainWindowProbe::page(window) == "Page 2 of 2");
      writeit::MainWindowProbe::undo(window);
      settle();
      writeit::MainWindowProbe::undo(window);
      settle();

      activate(footnote);
      writeit::Document noted = writeit::MainWindowProbe::doc(window);
      bool marker = false;
      for (const writeit::Run& run : noted.paragraphs[0].runs) {
        if (run.note == 1 && run.text == "1")
          marker = true;
      }
      CHECK(noted.notes.size() == 1 && marker);
      CHECK(writeit::MainWindowProbe::marker_raised(window));
      CHECK(writeit::MainWindowProbe::notes_focused(window));
      CHECK(writeit::MainWindowProbe::note_on_first_sheet(window));
      CHECK(writeit::MainWindowProbe::note_label_is(window, "1"));
      writeit::MainWindowProbe::undo(window);
      settle();
      CHECK(writeit::MainWindowProbe::doc(window).notes.empty());

      writeit::Document table_doc;
      writeit::insert_table(table_doc.paragraphs, 0, 2, 2, writeit::page_text_twips(table_doc.page));
      writeit::MainWindowProbe::load(window, table_doc);
      settle();
      CHECK(insert_row->get_sensitive());
      CHECK(delete_row->get_sensitive());
      const int rows_before = writeit::MainWindowProbe::doc(window).paragraphs[0].cell.rows;
      activate(insert_row);
      CHECK(writeit::MainWindowProbe::doc(window).paragraphs[0].cell.rows == rows_before + 1);
      writeit::MainWindowProbe::undo(window);
      settle();
      CHECK(writeit::MainWindowProbe::doc(window).paragraphs[0].cell.rows == rows_before);
      activate(delete_row);
      CHECK(writeit::MainWindowProbe::doc(window).paragraphs[0].cell.rows == rows_before - 1);
      writeit::MainWindowProbe::undo(window);
      settle();
      CHECK(writeit::MainWindowProbe::doc(window).paragraphs[0].cell.rows == rows_before);
    } else {
      for (int i = 0; i < 53; ++i)
        CHECK(false);
    }

    // The Table menu leaves the cell. Insert and delete still use that cell,
    // not the first row or the first column.
    if (insert_row && insert_column && delete_row && delete_column) {
      writeit::Document grid;
      writeit::insert_table(grid.paragraphs, 0, 2, 2, writeit::page_text_twips(grid.page));
      const char* names[] = {"A", "B", "C", "D"};
      for (int i = 0; i < 4; ++i) {
        writeit::Run run;
        run.text = names[i];
        grid.paragraphs[static_cast<size_t>(i)].runs.push_back(run);
      }
      writeit::MainWindowProbe::load(window, grid);
      settle();
      auto arm = [&] {
        writeit::MainWindowProbe::focus_table_cell(window, 0, 1, 1);
        writeit::MainWindowProbe::focus_body_quietly(window);
        settle();
      };
      arm();
      activate(insert_row);
      writeit::Document inserted_row = writeit::MainWindowProbe::doc(window);
      CHECK(inserted_row.paragraphs[0].cell.rows == 3);
      CHECK(cell_text(inserted_row, 0, 0) == "A" && cell_text(inserted_row, 0, 1) == "B" &&
            cell_text(inserted_row, 1, 0) == "C" && cell_text(inserted_row, 1, 1) == "D");
      CHECK(cell_text(inserted_row, 2, 0).empty() && cell_text(inserted_row, 2, 1).empty());
      writeit::MainWindowProbe::undo(window);
      settle();
      writeit::Document row_back = writeit::MainWindowProbe::doc(window);
      CHECK(row_back.paragraphs[0].cell.rows == 2 && cell_text(row_back, 1, 1) == "D");

      arm();
      activate(insert_column);
      writeit::Document inserted_column = writeit::MainWindowProbe::doc(window);
      CHECK(inserted_column.paragraphs[0].cell.columns == 3);
      CHECK(cell_text(inserted_column, 0, 0) == "A" && cell_text(inserted_column, 0, 1) == "B" &&
            cell_text(inserted_column, 0, 2).empty());
      CHECK(cell_text(inserted_column, 1, 0) == "C" && cell_text(inserted_column, 1, 1) == "D" &&
            cell_text(inserted_column, 1, 2).empty());
      writeit::MainWindowProbe::undo(window);
      settle();
      writeit::Document column_back = writeit::MainWindowProbe::doc(window);
      CHECK(column_back.paragraphs[0].cell.columns == 2 && cell_text(column_back, 1, 1) == "D");

      arm();
      activate(delete_row);
      writeit::Document deleted_row = writeit::MainWindowProbe::doc(window);
      CHECK(deleted_row.paragraphs[0].cell.rows == 1);
      CHECK(cell_text(deleted_row, 0, 0) == "A" && cell_text(deleted_row, 0, 1) == "B");
      writeit::MainWindowProbe::undo(window);
      settle();
      writeit::Document delete_row_back = writeit::MainWindowProbe::doc(window);
      CHECK(delete_row_back.paragraphs[0].cell.rows == 2 && cell_text(delete_row_back, 1, 1) == "D");

      arm();
      activate(delete_column);
      writeit::Document deleted_column = writeit::MainWindowProbe::doc(window);
      CHECK(deleted_column.paragraphs[0].cell.columns == 1);
      CHECK(cell_text(deleted_column, 0, 0) == "A" && cell_text(deleted_column, 1, 0) == "C");
      writeit::MainWindowProbe::undo(window);
      settle();
      writeit::Document delete_column_back = writeit::MainWindowProbe::doc(window);
      CHECK(delete_column_back.paragraphs[0].cell.columns == 2 &&
            cell_text(delete_column_back, 0, 1) == "B" &&
            cell_text(delete_column_back, 1, 1) == "D");
    } else {
      for (int i = 0; i < 14; ++i)
        CHECK(false);
    }

    // A table on a later page. The body insert is still the first line.
    // Insert and delete must leave the caret and the view on that table.
    if (insert_row && delete_row) {
      writeit::Document later;
      for (int i = 0; i < 120; ++i) {
        writeit::Paragraph paragraph;
        writeit::Run run;
        run.text = "line " + std::to_string(i);
        paragraph.runs.push_back(run);
        later.paragraphs.push_back(paragraph);
      }
      const size_t at = later.paragraphs.size();
      writeit::insert_table(later.paragraphs, at, 2, 2, writeit::page_text_twips(later.page));
      const char* names[] = {"A", "B", "C", "D"};
      for (int i = 0; i < 4; ++i) {
        writeit::Run run;
        run.text = names[i];
        later.paragraphs[at + static_cast<size_t>(i)].runs.push_back(run);
      }
      const int anchor = table_anchor(later);
      writeit::MainWindowProbe::load(window, later);
      settle();
      // Focusing the body scrolls the pasteboard to the top of the page.
      // The menu does not. Put the view back on the table before the command.
      auto arm = [&] {
        writeit::MainWindowProbe::focus_table_cell(window, 0, 1, 1);
        writeit::MainWindowProbe::focus_body_quietly(window);
        writeit::MainWindowProbe::scroll_to_end(window);
        settle();
      };
      arm();
      CHECK(writeit::MainWindowProbe::scroll_value(window) > 400.0);
      activate(insert_row);
      settle();
      writeit::Document inserted = writeit::MainWindowProbe::doc(window);
      CHECK(inserted.paragraphs[at].cell.rows == 3);
      CHECK(cell_text(inserted, 1, 1) == "D");
      CHECK(writeit::MainWindowProbe::cursor(window) == anchor);
      CHECK(writeit::MainWindowProbe::cell_focused(window, 0, 1, 1));
      CHECK(writeit::MainWindowProbe::scroll_value(window) > 400.0);
      writeit::MainWindowProbe::undo(window);
      settle();
      CHECK(writeit::MainWindowProbe::cursor(window) == anchor);
      arm();
      activate(delete_row);
      settle();
      writeit::Document deleted = writeit::MainWindowProbe::doc(window);
      CHECK(deleted.paragraphs[at].cell.rows == 1);
      CHECK(writeit::MainWindowProbe::cursor(window) == anchor);
      CHECK(writeit::MainWindowProbe::scroll_value(window) > 400.0);
    } else {
      for (int i = 0; i < 10; ++i)
        CHECK(false);
    }

    // Edit → Paste and the paste button edit the cell. The body caret is the
    // table anchor, and opening the menu leaves the cell. Ctrl+V is the
    // cell's own binding. The body's context menu stays on the body.
    const auto edit = menu_items(window, "_Edit");
    Gtk::MenuItem* cut = item_named(edit, "Cu_t");
    Gtk::MenuItem* copy = item_named(edit, "_Copy");
    Gtk::MenuItem* paste = item_named(edit, "_Paste");
    Gtk::MenuItem* erase = item_named(edit, "_Delete");
    Gtk::ToolButton* paste_tool = tool_named(window, "Paste");
    Gtk::ToolButton* cut_tool = tool_named(window, "Cut");
    Gtk::ToolButton* copy_tool = tool_named(window, "Copy");
    if (cut && copy && paste && erase && paste_tool && cut_tool && copy_tool) {
      writeit::Document doc;
      writeit::Paragraph before;
      writeit::Run run;
      run.text = "Before";
      before.runs.push_back(run);
      doc.paragraphs.push_back(before);
      const size_t at = doc.paragraphs.size();
      writeit::insert_table(doc.paragraphs, at, 2, 2, writeit::page_text_twips(doc.page));
      const char* names[] = {"A", "B", "C", "D"};
      for (int i = 0; i < 4; ++i) {
        writeit::Run cell_run;
        cell_run.text = names[i];
        doc.paragraphs[at + static_cast<size_t>(i)].runs.push_back(cell_run);
      }
      const int anchor = table_anchor(doc);
      writeit::MainWindowProbe::load(window, doc);
      settle();
      writeit::MainWindowProbe::caret(window, anchor);

      auto clipboard = Gtk::Clipboard::get();
      auto wait_body = [&](const std::string& text) {
        for (int i = 0; i < 50 && body_text(writeit::MainWindowProbe::doc(window)) != text; ++i)
          settle();
      };
      auto wait_cell = [&](int row, int column, const std::string& text) {
        for (int i = 0; i < 50 &&
                        cell_text(writeit::MainWindowProbe::doc(window), row, column) != text;
             ++i)
          settle();
      };
      auto wait_on = [&](Gtk::Widget* widget) {
        for (int i = 0; i < 50 && !(widget && widget->get_sensitive()); ++i)
          settle();
      };

      // No cell is being edited. Paste still belongs to the body.
      clipboard->set_text("NO");
      wait_on(paste);
      CHECK(paste->get_sensitive());
      activate(paste);
      wait_body("Before\nNO\n");
      writeit::Document body_pasted = writeit::MainWindowProbe::doc(window);
      CHECK(body_text(body_pasted) == "Before\nNO\n");
      CHECK(cell_text(body_pasted, 0, 0) == "A" && cell_text(body_pasted, 0, 1) == "B" &&
            cell_text(body_pasted, 1, 0) == "C" && cell_text(body_pasted, 1, 1) == "D");
      writeit::MainWindowProbe::undo(window);
      settle();
      writeit::Document cleared = writeit::MainWindowProbe::doc(window);
      CHECK(body_text(cleared) == "Before\n" && cell_text(cleared, 1, 1) == "D");

      clipboard->set_text("XY");
      wait_on(paste);
      writeit::MainWindowProbe::caret(window, anchor);
      writeit::MainWindowProbe::focus_table_cell(window, 0, 1, 1);
      writeit::MainWindowProbe::focus_body_quietly(window);
      settle();
      activate(paste);
      wait_cell(1, 1, "DXY");
      writeit::Document menu_pasted = writeit::MainWindowProbe::doc(window);
      CHECK(cell_text(menu_pasted, 0, 0) == "A" && cell_text(menu_pasted, 0, 1) == "B" &&
            cell_text(menu_pasted, 1, 0) == "C" && cell_text(menu_pasted, 1, 1) == "DXY");
      CHECK(body_text(menu_pasted) == "Before\n");
      CHECK(writeit::MainWindowProbe::cell_focused(window, 0, 1, 1));
      CHECK(writeit::MainWindowProbe::cursor(window) == anchor);
      writeit::MainWindowProbe::undo(window);
      settle();
      writeit::Document menu_back = writeit::MainWindowProbe::doc(window);
      CHECK(body_text(menu_back) == "Before\n" && cell_text(menu_back, 1, 1) == "D");

      clipboard->set_text("Z");
      wait_on(paste_tool);
      writeit::MainWindowProbe::caret(window, anchor);
      writeit::MainWindowProbe::focus_table_cell(window, 0, 0, 1);
      writeit::MainWindowProbe::focus_body_quietly(window);
      settle();
      g_signal_emit_by_name(paste_tool->gobj(), "clicked");
      settle();
      wait_cell(0, 1, "BZ");
      writeit::Document tool_pasted = writeit::MainWindowProbe::doc(window);
      CHECK(cell_text(tool_pasted, 0, 0) == "A" && cell_text(tool_pasted, 0, 1) == "BZ" &&
            cell_text(tool_pasted, 1, 0) == "C" && cell_text(tool_pasted, 1, 1) == "D");
      CHECK(body_text(tool_pasted) == "Before\n");
      CHECK(writeit::MainWindowProbe::cell_focused(window, 0, 0, 1));

      writeit::MainWindowProbe::focus_table_cell(window, 0, 1, 0);
      settle();
      CHECK(!cut->get_sensitive() && !copy->get_sensitive() && !erase->get_sensitive() &&
            !cut_tool->get_sensitive() && !copy_tool->get_sensitive());
      writeit::MainWindowProbe::select_cell(window, 0, 1, 0);
      settle();
      CHECK(cut->get_sensitive() && copy->get_sensitive() && erase->get_sensitive() &&
            cut_tool->get_sensitive() && copy_tool->get_sensitive() &&
            !writeit::MainWindowProbe::context_cut_on(window) &&
            !writeit::MainWindowProbe::context_copy_on(window));
      writeit::MainWindowProbe::focus_body_quietly(window);
      settle();
      CHECK(cut->get_sensitive() && copy->get_sensitive() && erase->get_sensitive());

      activate(copy);
      writeit::Document copied = writeit::MainWindowProbe::doc(window);
      CHECK(cell_text(copied, 1, 0) == "C" && body_text(copied) == "Before\n");
      CHECK(clipboard->wait_for_text() == "C");
      CHECK(writeit::MainWindowProbe::cell_focused(window, 0, 1, 0));

      writeit::MainWindowProbe::focus_body_quietly(window);
      settle();
      activate(erase);
      wait_cell(1, 0, "");
      writeit::Document deleted = writeit::MainWindowProbe::doc(window);
      CHECK(cell_text(deleted, 1, 0).empty() && cell_text(deleted, 0, 1) == "BZ" &&
            body_text(deleted) == "Before\n");
      CHECK(clipboard->wait_for_text() == "C");
      CHECK(writeit::MainWindowProbe::cell_focused(window, 0, 1, 0));

      writeit::MainWindowProbe::focus_table_cell(window, 0, 0, 0);
      settle();
      writeit::MainWindowProbe::select_cell(window, 0, 0, 0);
      settle();
      writeit::MainWindowProbe::focus_body_quietly(window);
      settle();
      CHECK(cut->get_sensitive());
      activate(cut);
      wait_cell(0, 0, "");
      writeit::Document cut_doc = writeit::MainWindowProbe::doc(window);
      CHECK(cell_text(cut_doc, 0, 0).empty() && cell_text(cut_doc, 0, 1) == "BZ" &&
            cell_text(cut_doc, 1, 1) == "D" && body_text(cut_doc) == "Before\n");
      CHECK(clipboard->wait_for_text() == "A");
      CHECK(writeit::MainWindowProbe::cell_focused(window, 0, 0, 0));

      // A table on a later page. Paste must not send the caret or the view
      // back to the first line.
      writeit::Document later;
      for (int i = 0; i < 120; ++i) {
        writeit::Paragraph paragraph;
        writeit::Run line;
        line.text = "line " + std::to_string(i);
        paragraph.runs.push_back(line);
        later.paragraphs.push_back(paragraph);
      }
      const size_t later_at = later.paragraphs.size();
      writeit::insert_table(later.paragraphs, later_at, 2, 2,
                            writeit::page_text_twips(later.page));
      for (int i = 0; i < 4; ++i) {
        writeit::Run cell_run;
        cell_run.text = names[i];
        later.paragraphs[later_at + static_cast<size_t>(i)].runs.push_back(cell_run);
      }
      const int later_anchor = table_anchor(later);
      writeit::MainWindowProbe::load(window, later);
      settle();
      clipboard->set_text("XY");
      wait_on(paste);
      writeit::MainWindowProbe::caret(window, later_anchor);
      writeit::MainWindowProbe::focus_table_cell(window, 0, 1, 1);
      writeit::MainWindowProbe::focus_body_quietly(window);
      writeit::MainWindowProbe::scroll_to_end(window);
      settle();
      CHECK(writeit::MainWindowProbe::scroll_value(window) > 400.0);
      activate(paste);
      wait_cell(1, 1, "DXY");
      writeit::Document later_pasted = writeit::MainWindowProbe::doc(window);
      CHECK(!later_pasted.paragraphs.empty() && !later_pasted.paragraphs[0].runs.empty() &&
            later_pasted.paragraphs[0].runs[0].text == "line 0" &&
            cell_text(later_pasted, 1, 1) == "DXY" &&
            writeit::MainWindowProbe::cursor(window) == later_anchor);
      CHECK(writeit::MainWindowProbe::cell_focused(window, 0, 1, 1) &&
            writeit::MainWindowProbe::scroll_value(window) > 400.0);
    } else {
      for (int i = 0; i < 28; ++i)
        CHECK(false);
    }

    const char* sample_rtf =
        "{\\rtf1\\ansi\\ansicpg1252\\deff0\\uc1\n"
        "{\\fonttbl{\\f0\\fswiss Sans;}}\n"
        "\\pard\\f0\\fs22\\b0\\i0\\ulnone This is a test\\par\n"
        "\\pard\\f0\\fs22\\b0\\i0\\ulnone\\par\n"
        "\\pard\\f0\\fs22\\b0\\i0\\ulnone This is a test\\par\n"
        "\\trowd\\trgaph108\\cellx2513\\cellx5026\\cellx7539\\cellx10054\\pard\\intbl\\f0\\fs22"
        "\\b0\\i0\\ulnone asdfasfdsafddsf\\tab sdadasdf\\tab asdasfdasfd\\tab \\tab dsfadfasdf"
        "\\tab \\cell\\pard\\intbl\\cell\\pard\\intbl\\cell\\pard\\intbl\\f0\\fs22\\b0\\i0"
        "\\ulnone sdasfsafdasdf\\tab \\tab wsdfasdfsafdasdfasdfasdfasdfasdfasdf\\cell\\row\n"
        "\\trowd\\trgaph108\\cellx2513\\cellx5026\\cellx7539\\cellx10054\\pard\\intbl\\cell"
        "\\pard\\intbl\\cell\\pard\\intbl\\cell\\pard\\intbl\\cell\\row\n"
        "\\trowd\\trgaph108\\cellx2513\\cellx5026\\cellx7539\\cellx10054\\pard\\intbl\\cell"
        "\\pard\\intbl\\f0\\fs22\\b0\\i0\\ulnone asdfasfasdfasfd\\cell\\pard\\intbl\\cell"
        "\\pard\\intbl\\cell\\row\n"
        "\\trowd\\trgaph108\\cellx2513\\cellx5026\\cellx7539\\cellx10054\\pard\\intbl\\cell"
        "\\pard\\intbl\\cell\\pard\\intbl\\cell\\pard\\intbl\\cell\\row\n"
        "\\pard\\par\n"
        "}";
    writeit::Document sample;
    CHECK(writeit::rtf_import(sample_rtf, sample));
    writeit::MainWindowProbe::load(window, sample);
    settle();
    CHECK(writeit::MainWindowProbe::table_line_holds_sheet(window));
    writeit::Document shown = writeit::MainWindowProbe::doc(window);
    if (!(shown == sample)) {
      std::cerr << "table grid capture " << shown.paragraphs.size() << " paragraphs, sample "
                << sample.paragraphs.size() << "\n";
      const size_t n = std::min(shown.paragraphs.size(), sample.paragraphs.size());
      for (size_t i = 0; i < n; ++i) {
        if (!(shown.paragraphs[i] == sample.paragraphs[i])) {
          std::cerr << "first mismatch at paragraph " << i << "\n";
          break;
        }
      }
    }
    CHECK(shown.paragraphs.size() == sample.paragraphs.size());
    CHECK(shown == sample);
    int cell_views = 0;
    bool top_left = false;
    if (body) {
      for (Gtk::Widget* child : body->get_children()) {
        walk(*child, [&](Gtk::Widget& widget) {
          auto* view = dynamic_cast<Gtk::TextView*>(&widget);
          if (!view || view == body)
            return;
          ++cell_views;
          const std::string text = view->get_buffer()->get_text().raw();
          if (text.compare(0, 15, "asdfasfdsafddsf") == 0)
            top_left = true;
        });
      }
    }
    CHECK(cell_views == 16);
    CHECK(top_left);

    Gtk::TextView* cell = nullptr;
    Gtk::TextView* other = nullptr;
    if (body) {
      for (Gtk::Widget* child : body->get_children()) {
        walk(*child, [&](Gtk::Widget& widget) {
          auto* view = dynamic_cast<Gtk::TextView*>(&widget);
          if (!view || view == body)
            return;
          const std::string text = view->get_buffer()->get_text().raw();
          if (!cell && text.compare(0, 15, "asdfasfdsafddsf") == 0)
            cell = view;
          else if (!other && text.empty())
            other = view;
        });
      }
    }
    CHECK(cell != nullptr);
    CHECK(other != nullptr);
    bool cell_focus = false;
    bool still_cell = false;
    bool other_focus = false;
    bool cell_left = false;
    bool same_doc = false;
    int near = -1;
    int far = -1;
    if (cell && other) {
      auto text_win = cell->get_window(Gtk::TEXT_WINDOW_TEXT);
      const int width = text_win ? text_win->get_width() : 0;
      click_view(*cell, 4, 4, 1000);
      cell_focus = cell->has_focus();
      near = cell->get_buffer()->get_insert()->get_iter().get_offset();
      click_view(*cell, std::max(8, width - 4), 4, 2000);
      still_cell = cell->has_focus();
      far = cell->get_buffer()->get_insert()->get_iter().get_offset();
      click_view(*other, 4, 4, 3000);
      other_focus = other->has_focus();
      cell_left = !cell->has_focus();
      same_doc = writeit::MainWindowProbe::doc(window) == sample;
    }
    CHECK(cell_focus);
    CHECK(still_cell);
    CHECK(near != far);
    CHECK(other_focus);
    CHECK(cell_left);
    CHECK(same_doc);

    int alloc_before = -1;
    std::vector<int> widths_before;
    if (cell && cell->get_parent())
      alloc_before = cell->get_parent()->get_allocated_width();
    for (const writeit::Paragraph& paragraph : writeit::MainWindowProbe::doc(window).paragraphs) {
      if (paragraph.cell.table != 0 && paragraph.cell.widths.size() >= 2) {
        widths_before = paragraph.cell.widths;
        break;
      }
    }
    if (cell) {
      auto text_win = cell->get_window(Gtk::TEXT_WINDOW_TEXT);
      const int width = text_win ? text_win->get_width() : 0;
      if (width > 8)
        drag_view(*cell, width - 2, 4, -40, 4000);
    }
    settle();
    std::vector<int> widths_after;
    for (const writeit::Paragraph& paragraph : writeit::MainWindowProbe::doc(window).paragraphs) {
      if (paragraph.cell.table != 0 && paragraph.cell.widths.size() >= 2) {
        widths_after = paragraph.cell.widths;
        break;
      }
    }
    Gtk::TextView* resized = nullptr;
    if (body) {
      for (Gtk::Widget* child : body->get_children()) {
        walk(*child, [&](Gtk::Widget& widget) {
          auto* view = dynamic_cast<Gtk::TextView*>(&widget);
          if (!view || view == body || resized)
            return;
          if (view->get_buffer()->get_text().raw().compare(0, 15, "asdfasfdsafddsf") == 0)
            resized = view;
        });
      }
    }
    int alloc_after = -1;
    if (resized && resized->get_parent())
      alloc_after = resized->get_parent()->get_allocated_width();
    const bool narrower = widths_after.size() >= 2 && widths_before.size() >= 2 &&
                          widths_after[0] < widths_before[0];
    const bool pair_kept = widths_after.size() >= 2 && widths_before.size() >= 2 &&
                           widths_after[0] + widths_after[1] == widths_before[0] + widths_before[1];
    CHECK(narrower);
    CHECK(pair_kept);
    CHECK(alloc_after > 0 && alloc_after < alloc_before);

    Gtk::TextView* noted_cell = nullptr;
    if (body) {
      for (Gtk::Widget* child : body->get_children()) {
        walk(*child, [&](Gtk::Widget& widget) {
          auto* view = dynamic_cast<Gtk::TextView*>(&widget);
          if (!view || view == body || noted_cell)
            return;
          if (view->get_buffer()->get_text().raw().compare(0, 15, "asdfasfdsafddsf") == 0)
            noted_cell = view;
        });
      }
    }
    if (noted_cell)
      click_view(*noted_cell, 4, 4, 6000);
    activate(footnote);
    bool cell_marker = false;
    const writeit::Document cell_note = writeit::MainWindowProbe::doc(window);
    for (const writeit::Paragraph& paragraph : cell_note.paragraphs) {
      if (paragraph.cell.table == 0)
        continue;
      for (const writeit::Run& run : paragraph.runs) {
        if (run.note == 1 && run.text == "1")
          cell_marker = true;
      }
    }
    CHECK(cell_note.notes.size() == 1 && cell_marker);
    writeit::MainWindowProbe::undo(window);
    settle();

    // 1×1 red PNG. The header length is zero bytes, which the old string
    // loader stopped on and replaced with the 48×24 stand-in.
    const unsigned char png[] = {
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44,
        0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x02, 0x00, 0x00, 0x00, 0x90,
        0x77, 0x53, 0xde, 0x00, 0x00, 0x00, 0x0c, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0xf8,
        0xcf, 0xc0, 0x00, 0x00, 0x03, 0x01, 0x01, 0x00, 0xc9, 0xfe, 0x92, 0xef, 0x00, 0x00,
        0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};
    writeit::Document picture_doc;
    writeit::Paragraph paragraph;
    writeit::Run run;
    writeit::Image image;
    image.type = "png";
    image.data.assign(reinterpret_cast<const char*>(png), sizeof png);
    run.image = std::move(image);
    paragraph.runs.push_back(std::move(run));
    picture_doc.paragraphs.push_back(std::move(paragraph));
    writeit::MainWindowProbe::load(window, picture_doc);
    settle();
    int picture_w = 0;
    int picture_h = 0;
    CHECK(writeit::MainWindowProbe::first_pixbuf(window, picture_w, picture_h));
    CHECK(picture_w == 1 && picture_h == 1);

    // Insert into a document that has just been opened, and into one that
    // has text but no table. Retagging sees no change in the text and used
    // to leave the buffer without the picture.
    const std::string png_bytes(reinterpret_cast<const char*>(png), sizeof png);
    auto with_picture = [&](writeit::Document doc) {
      if (doc.paragraphs.empty())
        doc.paragraphs.emplace_back();
      writeit::Run image_run;
      writeit::Image stored;
      stored.type = "png";
      stored.data = png_bytes;
      image_run.image = std::move(stored);
      doc.paragraphs.back().runs.push_back(std::move(image_run));
      return doc;
    };
    auto kept_bytes = [&](const writeit::Document& doc) {
      for (const writeit::Paragraph& paragraph : doc.paragraphs) {
        for (const writeit::Run& run : paragraph.runs) {
          if (run.image && run.image->data == png_bytes)
            return true;
        }
      }
      return false;
    };

    writeit::MainWindowProbe::load(window, writeit::blank_document("Sans", 11));
    settle();
    {
      writeit::Document before = writeit::MainWindowProbe::doc(window);
      writeit::Document after = with_picture(before);
      writeit::MainWindowProbe::commit(window, before, after, 1);
    }
    settle();
    int fresh_w = 0;
    int fresh_h = 0;
    CHECK(writeit::MainWindowProbe::first_pixbuf(window, fresh_w, fresh_h));
    CHECK(fresh_w == 1 && fresh_h == 1);
    CHECK(kept_bytes(writeit::MainWindowProbe::doc(window)));

    writeit::Document hello = writeit::blank_document("Sans", 11);
    writeit::Run hello_run;
    hello_run.text = "Hello";
    hello_run.font = "Sans";
    hello_run.size = 11;
    hello.paragraphs[0].runs.push_back(std::move(hello_run));
    writeit::MainWindowProbe::load(window, hello);
    settle();
    const int hello_at = body ? body->get_buffer()->get_char_count() : 0;
    {
      writeit::Document before = writeit::MainWindowProbe::doc(window);
      writeit::Document after = with_picture(before);
      writeit::MainWindowProbe::commit(window, before, after, hello_at + 1);
    }
    settle();
    int typed_w = 0;
    int typed_h = 0;
    CHECK(writeit::MainWindowProbe::first_pixbuf(window, typed_w, typed_h));
    CHECK(typed_w == 1 && typed_h == 1);
    const writeit::Document typed = writeit::MainWindowProbe::doc(window);
    bool kept_hello = false;
    if (!typed.paragraphs.empty()) {
      for (const writeit::Run& run : typed.paragraphs[0].runs) {
        if (run.text == "Hello")
          kept_hello = true;
      }
    }
    CHECK(kept_hello && kept_bytes(typed));

    // Insert → Picture uses the cell caret. The file dialog has taken the
    // keyboard, and the body caret is still the first line.
    writeit::MainWindowProbe::load(window, hello);
    settle();
    writeit::MainWindowProbe::caret(window, 5);
    writeit::MainWindowProbe::insert_picture(window, png_bytes);
    settle();
    int body_w = 0;
    int body_h = 0;
    CHECK(writeit::MainWindowProbe::first_pixbuf(window, body_w, body_h) && body_w == 1 &&
          body_h == 1 && writeit::MainWindowProbe::cursor(window) == 6);
    {
      const writeit::Document placed = writeit::MainWindowProbe::doc(window);
      bool hello_then_picture = false;
      if (!placed.paragraphs.empty() && placed.paragraphs[0].runs.size() >= 2 &&
          placed.paragraphs[0].runs[0].text == "Hello" && placed.paragraphs[0].runs[1].image &&
          placed.paragraphs[0].runs[1].image->data == png_bytes)
        hello_then_picture = true;
      CHECK(hello_then_picture);
    }

    writeit::Document grid;
    writeit::Paragraph before;
    writeit::Run before_run;
    before_run.text = "Before";
    before.runs.push_back(before_run);
    grid.paragraphs.push_back(before);
    const size_t grid_at = grid.paragraphs.size();
    writeit::insert_table(grid.paragraphs, grid_at, 2, 2, writeit::page_text_twips(grid.page));
    const char* letters[] = {"A", "B", "C", "D"};
    for (int i = 0; i < 4; ++i) {
      writeit::Run cell_run;
      cell_run.text = letters[i];
      grid.paragraphs[grid_at + static_cast<size_t>(i)].runs.push_back(cell_run);
    }
    const int grid_anchor = table_anchor(grid);
    writeit::MainWindowProbe::load(window, grid);
    settle();
    writeit::MainWindowProbe::focus_table_cell(window, 0, 1, 1);
    settle();
    writeit::MainWindowProbe::focus_body_quietly(window);
    settle();
    writeit::MainWindowProbe::insert_picture(window, png_bytes);
    settle();
    {
      const writeit::Document pictured = writeit::MainWindowProbe::doc(window);
      CHECK(cell_marks(pictured, 0, 0, png_bytes) == "A" &&
            cell_marks(pictured, 0, 1, png_bytes) == "B" &&
            cell_marks(pictured, 1, 0, png_bytes) == "C" &&
            cell_marks(pictured, 1, 1, png_bytes) == "D\x01");
      CHECK(body_text(pictured) == "Before\n" && !body_has_image(pictured));
      CHECK(writeit::MainWindowProbe::cell_focused(window, 0, 1, 1) &&
            writeit::MainWindowProbe::cell_caret(window, 0, 1, 1) == 2 &&
            writeit::MainWindowProbe::cursor(window) == grid_anchor);
    }
    writeit::MainWindowProbe::undo(window);
    settle();
    {
      const writeit::Document undone = writeit::MainWindowProbe::doc(window);
      CHECK(cell_marks(undone, 1, 1, png_bytes) == "D" && body_text(undone) == "Before\n" &&
            !body_has_image(undone));
    }

    grid.paragraphs[grid_at + 1].runs.clear();
    writeit::Run wide;
    wide.text = "WXYZ";
    grid.paragraphs[grid_at + 1].runs.push_back(wide);
    writeit::MainWindowProbe::load(window, grid);
    settle();
    writeit::MainWindowProbe::focus_table_cell(window, 0, 0, 1);
    settle();
    writeit::MainWindowProbe::place_cell_caret(window, 0, 0, 1, 2);
    settle();
    writeit::MainWindowProbe::focus_body_quietly(window);
    settle();
    writeit::MainWindowProbe::insert_picture(window, png_bytes);
    settle();
    {
      const writeit::Document mid = writeit::MainWindowProbe::doc(window);
      CHECK(cell_marks(mid, 0, 0, png_bytes) == "A" &&
            cell_marks(mid, 0, 1, png_bytes) == "WX\x01YZ" &&
            cell_marks(mid, 1, 0, png_bytes) == "C" &&
            cell_marks(mid, 1, 1, png_bytes) == "D");
      CHECK(writeit::MainWindowProbe::cell_focused(window, 0, 0, 1) &&
            writeit::MainWindowProbe::cell_caret(window, 0, 0, 1) == 3 &&
            body_text(mid) == "Before\n" && !body_has_image(mid) &&
            writeit::MainWindowProbe::cursor(window) == grid_anchor);
    }

    writeit::Document later;
    for (int i = 0; i < 120; ++i) {
      writeit::Paragraph paragraph;
      writeit::Run line;
      line.text = "line " + std::to_string(i);
      paragraph.runs.push_back(line);
      later.paragraphs.push_back(paragraph);
    }
    const size_t later_at = later.paragraphs.size();
    writeit::insert_table(later.paragraphs, later_at, 2, 2, writeit::page_text_twips(later.page));
    for (int i = 0; i < 4; ++i) {
      writeit::Run cell_run;
      cell_run.text = letters[i];
      later.paragraphs[later_at + static_cast<size_t>(i)].runs.push_back(cell_run);
    }
    const int later_anchor = table_anchor(later);
    writeit::MainWindowProbe::load(window, later);
    settle();
    writeit::MainWindowProbe::focus_table_cell(window, 0, 1, 1);
    settle();
    writeit::MainWindowProbe::focus_body_quietly(window);
    writeit::MainWindowProbe::scroll_to_end(window);
    settle();
    CHECK(writeit::MainWindowProbe::scroll_value(window) > 400.0);
    writeit::MainWindowProbe::insert_picture(window, png_bytes);
    settle();
    {
      const writeit::Document pictured = writeit::MainWindowProbe::doc(window);
      CHECK(!pictured.paragraphs.empty() && !pictured.paragraphs[0].runs.empty() &&
            pictured.paragraphs[0].runs[0].text == "line 0" &&
            cell_marks(pictured, 1, 1, png_bytes) == "D\x01" && !body_has_image(pictured) &&
            writeit::MainWindowProbe::cursor(window) == later_anchor &&
            writeit::MainWindowProbe::cell_focused(window, 0, 1, 1) &&
            writeit::MainWindowProbe::scroll_value(window) > 400.0);
    }

    auto note_plain = [](const writeit::Document& doc, size_t index) {
      std::string out;
      if (index >= doc.notes.size())
        return out;
      for (size_t i = 0; i < doc.notes[index].size(); ++i) {
        if (i)
          out.push_back('\n');
        for (const writeit::Run& run : doc.notes[index][i].runs)
          out += run.text;
      }
      return out;
    };
    auto line = [](const std::string& text) {
      writeit::Paragraph paragraph;
      writeit::Run run;
      run.text = text;
      paragraph.runs.push_back(run);
      return paragraph;
    };
    writeit::Paragraph marker_paragraph;
    writeit::Run marker;
    marker.text = "1";
    marker.note = 1;
    marker_paragraph.runs.push_back(marker);

    writeit::Document noted;
    noted.paragraphs.push_back(marker_paragraph);
    noted.notes.push_back({line("alpha"), line("beta")});
    noted.notes.push_back({line("gamma")});
    writeit::MainWindowProbe::load(window, noted);
    settle();
    {
      const writeit::Document shown = writeit::MainWindowProbe::doc(window);
      CHECK(note_plain(shown, 0) == "alpha\nbeta" && note_plain(shown, 1) == "gamma");
    }
    CHECK(writeit::MainWindowProbe::note_number(window, 0) == "1" &&
          writeit::MainWindowProbe::note_number(window, 1) == "2");
    const int top = writeit::MainWindowProbe::note_text_y(window, 0);
    const int beta = writeit::MainWindowProbe::note_text_y(window, 6);
    const int gamma = writeit::MainWindowProbe::note_text_y(window, 12);
    const int number_one = writeit::MainWindowProbe::note_number_y(window, 0);
    const int number_two = writeit::MainWindowProbe::note_number_y(window, 1);
    CHECK(number_one >= 0 && top >= 0 && std::abs(number_one - top) <= 4);
    CHECK(number_two >= 0 && gamma >= 0 && std::abs(number_two - gamma) <= 4);
    CHECK(beta >= 0 && number_two > beta + 8);

    writeit::MainWindowProbe::set_notes(window, "one\ntwo");
    settle();
    {
      const writeit::Document joined = writeit::MainWindowProbe::doc(window);
      CHECK(joined.notes.size() == 1 && note_plain(joined, 0) == "one\ntwo");
    }
    writeit::MainWindowProbe::set_notes(window, "one\n\ntwo");
    settle();
    {
      const writeit::Document split = writeit::MainWindowProbe::doc(window);
      CHECK(split.notes.size() == 2 && note_plain(split, 0) == "one" &&
            note_plain(split, 1) == "two");
    }

    std::string long_note;
    for (int i = 0; i < 40; ++i)
      long_note += "word ";
    writeit::Document wrapped;
    wrapped.paragraphs.push_back(marker_paragraph);
    wrapped.notes.push_back({line(long_note)});
    wrapped.notes.push_back({line("Tail")});
    writeit::MainWindowProbe::load(window, wrapped);
    settle();
    const int wrap_at = writeit::MainWindowProbe::note_wrap_offset(window);
    const int wrap_y = writeit::MainWindowProbe::note_text_y(window, wrap_at);
    const int tail_at = static_cast<int>(long_note.size()) + 2;
    const int tail_y = writeit::MainWindowProbe::note_text_y(window, tail_at);
    const int wrapped_number = writeit::MainWindowProbe::note_number_y(window, 1);
    CHECK(wrap_at > 0 && wrap_y > writeit::MainWindowProbe::note_text_y(window, 0) + 8);
    CHECK(wrapped_number >= 0 && tail_y >= 0 && std::abs(wrapped_number - tail_y) <= 4);
    CHECK(wrapped_number > wrap_y + 8);

    // The note sits at the foot of a sheet taller than the window. A key
    // there keeps the pasteboard on that foot instead of jumping to the
    // marker and back.
    window.resize(800, 500);
    settle();
    writeit::MainWindowProbe::scroll_to_end(window);
    settle();
    const double foot = writeit::MainWindowProbe::scroll_value(window);
    const double lowest = writeit::MainWindowProbe::type_note(window, "a");
    settle();
    const double stayed = writeit::MainWindowProbe::scroll_value(window);
    CHECK(foot > 150.0 && lowest > foot - 40.0);
    CHECK(stayed > foot - 40.0);
    // The next key finds the body caret already marked to be followed.
    const double again = writeit::MainWindowProbe::type_note(window, "b");
    settle();
    const double still = writeit::MainWindowProbe::scroll_value(window);
    CHECK(again > foot - 40.0 && still > foot - 40.0);

    // A burst that rewraps the note used to collapse the band to one line
    // and drop it down the page, then grow it back.
    const int band_y = writeit::MainWindowProbe::note_band_y(window);
    const int band_h = writeit::MainWindowProbe::note_band_height(window);
    const double band_at = writeit::MainWindowProbe::scroll_value(window);
    const auto burst = writeit::MainWindowProbe::type_note_motion(window, std::string(200, 'W'));
    CHECK(band_h > 20 && burst.low_height >= band_h - 4);
    CHECK(burst.high_y <= band_y + 4);
    CHECK(burst.low_scroll > band_at - 40.0 && burst.end_scroll > band_at - 40.0);
    CHECK(!burst.dipped);

    // The same dip on the insert that first wraps the note. The band reaches
    // its full height and then a layout pass reports one line.
    writeit::Document fresh_note;
    fresh_note.paragraphs.push_back(marker_paragraph);
    fresh_note.notes.push_back({line("Hi")});
    writeit::MainWindowProbe::load(window, fresh_note);
    window.resize(800, 500);
    settle();
    writeit::MainWindowProbe::scroll_to_end(window);
    settle();
    const double fresh_at = writeit::MainWindowProbe::scroll_value(window);
    const auto grown = writeit::MainWindowProbe::type_note_motion(window, std::string(200, 'W'));
    CHECK(!grown.dipped);
    CHECK(grown.low_scroll > fresh_at - 40.0 && grown.end_scroll > fresh_at - 40.0);

    // A table and a picture make a note key rebuild the body. The page
    // clears, the scroll goes to the top, and the note is no longer where
    // it was typed.
    writeit::Document heavy;
    for (int i = 0; i < 60; ++i) {
      writeit::Paragraph paragraph;
      writeit::Run run;
      run.text = "A line of the letter, long enough to fill the sheet.";
      paragraph.runs.push_back(run);
      heavy.paragraphs.push_back(std::move(paragraph));
    }
    const size_t table_at = heavy.paragraphs.size();
    writeit::insert_table(heavy.paragraphs, table_at, 2, 2, writeit::page_text_twips(heavy.page));
    writeit::Paragraph pictured;
    writeit::Run image_run;
    writeit::Image stored;
    stored.type = "png";
    stored.data = png_bytes;
    image_run.image = std::move(stored);
    pictured.runs.push_back(std::move(image_run));
    heavy.paragraphs.push_back(std::move(pictured));
    writeit::Paragraph marker_line;
    writeit::Run marker_run;
    marker_run.text = "1";
    marker_run.note = 1;
    marker_line.runs.push_back(marker_run);
    heavy.paragraphs.push_back(std::move(marker_line));
    heavy.notes.push_back({line("Noted")});
    CHECK(heavy.paragraphs[table_at].cell.table != 0);
    writeit::MainWindowProbe::load(window, heavy);
    window.resize(800, 500);
    settle();
    writeit::MainWindowProbe::scroll_to_end(window);
    settle();
    const double foot2 = writeit::MainWindowProbe::scroll_value(window);
    const auto keyed = writeit::MainWindowProbe::type_note_motion(window, "x");
    CHECK(foot2 > 150.0);
    CHECK(keyed.low_scroll > foot2 - 40.0 && keyed.end_scroll > foot2 - 40.0);
    CHECK(keyed.low_body > 100);
    {
      const writeit::Document typed = writeit::MainWindowProbe::doc(window);
      CHECK(note_plain(typed, 0).find('x') != std::string::npos);
    }
    writeit::MainWindowProbe::undo(window);
    settle();
    {
      const writeit::Document undone = writeit::MainWindowProbe::doc(window);
      CHECK(note_plain(undone, 0) == "Noted" &&
            writeit::MainWindowProbe::scroll_value(window) > foot2 - 40.0);
    }

    // A marker on a later sheet keeps its note on that sheet. The column on
    // the first sheet used to hold every note.
    writeit::Document paged;
    for (int i = 0; i < 80; ++i) {
      writeit::Paragraph paragraph;
      writeit::Run run;
      run.text = "A line of the letter, long enough to fill the sheet.";
      paragraph.runs.push_back(run);
      paged.paragraphs.push_back(std::move(paragraph));
    }
    writeit::Run first_mark;
    first_mark.text = "1";
    first_mark.note = 1;
    paged.paragraphs.front().runs.push_back(first_mark);
    writeit::Run second_mark;
    second_mark.text = "2";
    second_mark.note = 2;
    paged.paragraphs.back().runs.push_back(second_mark);
    paged.notes.push_back({line("Alpha")});
    paged.notes.push_back({line("Beta")});
    writeit::MainWindowProbe::load(window, paged);
    window.resize(800, 500);
    settle();
    settle();
    const int mark_one = writeit::MainWindowProbe::marker_overlay_y(window, 1);
    const int mark_two = writeit::MainWindowProbe::marker_overlay_y(window, 2);
    int band_one = -1;
    int band_two = -1;
    std::string text_one;
    std::string text_two;
    std::string number_one_band;
    std::string number_two_band;
    const bool spot_one =
        writeit::MainWindowProbe::note_band_at(window, 1, band_one, text_one, number_one_band);
    const bool spot_two =
        writeit::MainWindowProbe::note_band_at(window, 2, band_two, text_two, number_two_band);
    CHECK(spot_one && spot_two);
    CHECK(mark_one >= 0 && mark_two > mark_one + 400);
    CHECK(band_one > mark_one && band_one < mark_two);
    CHECK(band_two > mark_two - 40 && band_two < mark_two + 1600);
    CHECK(text_one.find("Alpha") != std::string::npos && text_one.find("Beta") == std::string::npos);
    CHECK(text_two.find("Beta") != std::string::npos && text_two.find("Alpha") == std::string::npos);
    CHECK(number_one_band == "1" && number_two_band == "2");
    writeit::MainWindowProbe::type_in_note(window, 2, "z");
    settle();
    int band_two_after = -1;
    std::string text_two_after;
    std::string number_two_after;
    writeit::MainWindowProbe::note_band_at(window, 2, band_two_after, text_two_after,
                                           number_two_after);
    const int mark_two_after = writeit::MainWindowProbe::marker_overlay_y(window, 2);
    CHECK(text_two_after.find('z') != std::string::npos);
    CHECK(band_two_after > mark_two_after - 40);
    CHECK(writeit::MainWindowProbe::doc(window).paragraphs.size() > 40);
  }
  return suite_test::done("page-ui", kChecks);
}
