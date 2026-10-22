/* SPDX-License-Identifier: Unlicense */

// The format painter copies one character's look and its paragraph format.
// A drag paints the characters it covers and every paragraph it touches.
// Inside a table it also copies that cell's shading. One click paints once.
// A second click within the double-click time keeps painting until Escape.

#include "check.hpp"
#include "document.hpp"
#include "main_window.hpp"

#include <glib.h>
#include <glib/gstdio.h>
#include <glibmm/miscutils.h>
#include <gtkmm.h>

#include <algorithm>
#include <cstdlib>
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

  static void undo(MainWindow& window)
  {
    window.undo();
  }

  static bool can_undo(MainWindow& window)
  {
    return window.undo_.can_undo();
  }

  static Gtk::TextView& body(MainWindow& window)
  {
    return window.text_;
  }

  static Gtk::ToggleToolButton* painter(MainWindow& window)
  {
    return window.painter_tool_;
  }

  static Gtk::Widget* painter_face(MainWindow& window)
  {
    return window.painter_tool_ ? window.painter_tool_->get_child() : nullptr;
  }

  static bool painter_on(MainWindow& window)
  {
    return window.painter_on_;
  }

  static bool painter_sticky(MainWindow& window)
  {
    return window.painter_sticky_;
  }

  static void apply(MainWindow& window, Gtk::TextView& view)
  {
    window.apply_painter(view);
  }

  static void queue(MainWindow& window, Gtk::TextView& view)
  {
    window.queue_painter(view);
  }

  static bool escape(MainWindow& window)
  {
    GdkEventKey key{};
    key.type = GDK_KEY_PRESS;
    key.keyval = GDK_KEY_Escape;
    return window.on_key_press_event(&key);
  }

  static Gtk::TextView* cell_view(MainWindow& window, int row, int column)
  {
    return window.nth_cell_view(0, row, column);
  }

  static void select(Gtk::TextView& view, int from, int to)
  {
    auto buffer = view.get_buffer();
    if (!buffer)
      return;
    const int count = buffer->get_char_count();
    from = std::max(0, std::min(from, count));
    to = std::max(0, std::min(to, count));
    buffer->select_range(buffer->get_iter_at_offset(from), buffer->get_iter_at_offset(to));
  }

  static void caret(Gtk::TextView& view, int offset)
  {
    auto buffer = view.get_buffer();
    if (!buffer)
      return;
    const int count = buffer->get_char_count();
    const int at = std::max(0, std::min(offset, count));
    buffer->place_cursor(buffer->get_iter_at_offset(at));
  }

  static void focus_cell(MainWindow& window, int row, int column)
  {
    Gtk::TextView* view = cell_view(window, row, column);
    if (!view)
      return;
    view->grab_focus();
  }
};

}  // namespace writeit

namespace {

constexpr unsigned kCharacterDirect = writeit::kDirectFont | writeit::kDirectSize |
                                      writeit::kDirectBold | writeit::kDirectItalic |
                                      writeit::kDirectUnderline | writeit::kDirectColor |
                                      writeit::kDirectHighlight | writeit::kDirectStrike |
                                      writeit::kDirectScript;

constexpr unsigned kParagraphDirect = writeit::kDirectLeft | writeit::kDirectAlign |
                                      writeit::kDirectBefore | writeit::kDirectAfter |
                                      writeit::kDirectLine | writeit::kDirectBorders |
                                      writeit::kDirectShading | writeit::kDirectTabs;

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

writeit::Run text_run(const char* text)
{
  writeit::Run run;
  run.text = text;
  return run;
}

writeit::Run rich_run(const char* text)
{
  writeit::Run run = text_run(text);
  run.font = "Times";
  run.size = 18;
  run.bold = true;
  run.italic = true;
  run.underline = true;
  run.color = writeit::Ink::Red;
  run.highlight = writeit::Ink::Yellow;
  run.strike = true;
  run.script = writeit::Script::Super;
  run.direct = kCharacterDirect;
  return run;
}

bool rich_look(const writeit::Run& run)
{
  return run.font == "Times" && run.size == 18 && run.bold && run.italic && run.underline &&
         run.color == writeit::Ink::Red && run.highlight == writeit::Ink::Yellow && run.strike &&
         run.script == writeit::Script::Super && run.direct == kCharacterDirect && run.note == 0 &&
         !run.image;
}

void dress(writeit::Paragraph& paragraph)
{
  paragraph.heading = 1;
  paragraph.style = "Heading 1";
  paragraph.indents.left = 720;
  paragraph.align = writeit::Align::Center;
  paragraph.spacing.before = 200;
  paragraph.spacing.after = 100;
  paragraph.spacing.rule = writeit::LineRule::Exactly;
  paragraph.spacing.value = 360;
  paragraph.shading.pattern = 10000;
  paragraph.shading.background = writeit::Ink::Yellow;
  paragraph.borders.top.style = writeit::BorderStyle::Single;
  paragraph.borders.top.width = 10;
  paragraph.borders.top.color = writeit::Ink::Red;
  writeit::TabStop tab;
  tab.position = 1440;
  tab.align = writeit::TabAlign::Center;
  tab.leader = writeit::TabLeader::Dot;
  paragraph.tabs = {tab};
  paragraph.list.kind = writeit::ListKind::Bullet;
  paragraph.list.level = 1;
  paragraph.direct = kParagraphDirect;
}

bool dressed(const writeit::Paragraph& paragraph)
{
  return paragraph.heading == 1 && paragraph.style == "Heading 1" &&
         paragraph.indents.left == 720 && paragraph.align == writeit::Align::Center &&
         paragraph.spacing.before == 200 && paragraph.spacing.after == 100 &&
         paragraph.spacing.rule == writeit::LineRule::Exactly && paragraph.spacing.value == 360 &&
         paragraph.shading.pattern == 10000 &&
         paragraph.shading.background == writeit::Ink::Yellow &&
         paragraph.borders.top.style == writeit::BorderStyle::Single &&
         paragraph.borders.top.width == 10 && paragraph.borders.top.color == writeit::Ink::Red &&
         paragraph.tabs.size() == 1 && paragraph.tabs[0].position == 1440 &&
         paragraph.tabs[0].align == writeit::TabAlign::Center &&
         paragraph.tabs[0].leader == writeit::TabLeader::Dot &&
         paragraph.list.kind == writeit::ListKind::Bullet && paragraph.list.level == 1 &&
         paragraph.direct == kParagraphDirect;
}

bool plain_look(const writeit::Paragraph& paragraph)
{
  return paragraph.heading == 0 && paragraph.style == writeit::kNormalStyle &&
         paragraph.indents.left == 0 && paragraph.align == writeit::Align::Left &&
         paragraph.spacing.before == 0 && paragraph.spacing.after == 0 &&
         paragraph.spacing.rule == writeit::LineRule::Single && paragraph.tabs.empty() &&
         paragraph.list.kind == writeit::ListKind::None &&
         writeit::shading_plain(paragraph.shading) && writeit::borders_plain(paragraph.borders);
}

writeit::Shading solid(writeit::Ink ink)
{
  writeit::Shading shading;
  shading.pattern = writeit::kMaxShading;
  shading.background = ink;
  return shading;
}

bool filled(const writeit::Shading& shading, writeit::Ink ink)
{
  return shading.pattern == 10000 && shading.background == ink &&
         shading.foreground == writeit::Ink::Auto;
}

std::string joined(const writeit::Paragraph& paragraph)
{
  std::string out;
  for (const writeit::Run& run : paragraph.runs)
    out += run.text;
  return out;
}

const writeit::Paragraph* body_named(const writeit::Document& doc, const char* text)
{
  for (const writeit::Paragraph& paragraph : doc.paragraphs) {
    if (paragraph.cell.table == 0 && joined(paragraph) == text)
      return &paragraph;
  }
  return nullptr;
}

writeit::Paragraph* cell_at(writeit::Document& doc, int row, int column, int which)
{
  int seen = 0;
  for (writeit::Paragraph& paragraph : doc.paragraphs) {
    if (paragraph.cell.table == 0 || paragraph.cell.row != row || paragraph.cell.column != column)
      continue;
    if (seen == which)
      return &paragraph;
    ++seen;
  }
  return nullptr;
}

bool span_is(const std::vector<writeit::PaintSpan>& spans, size_t n, size_t index, int from, int to)
{
  return n < spans.size() && spans[n].index == index && spans[n].from == from && spans[n].to == to;
}

writeit::Paragraph paragraph_of(const char* text)
{
  writeit::Paragraph paragraph;
  paragraph.runs = {text_run(text)};
  return paragraph;
}

writeit::Document hello_world()
{
  writeit::Document doc = writeit::blank_document("Sans", 11);
  doc.paragraphs[0].runs = {text_run("Hello")};
  doc.paragraphs.push_back(paragraph_of("World"));
  return doc;
}

writeit::PainterFormat brush()
{
  writeit::PainterFormat format;
  format.character = rich_run("");
  dress(format.paragraph);
  return format;
}

bool press_face(Gtk::Widget& widget, guint32 time, GdkEventType type)
{
  auto window = widget.get_window();
  if (!window)
    return false;
  GdkEvent* event = gdk_event_new(type);
  event->button.window = GDK_WINDOW(g_object_ref(window->gobj()));
  event->button.send_event = TRUE;
  event->button.time = time;
  event->button.x = 1;
  event->button.y = 1;
  int origin_x = 0;
  int origin_y = 0;
  window->get_origin(origin_x, origin_y);
  event->button.x_root = origin_x + 1;
  event->button.y_root = origin_y + 1;
  event->button.button = 1;
  event->button.state = type == GDK_BUTTON_RELEASE ? GDK_BUTTON1_MASK : 0;
  auto pointer = window->get_display()->get_default_seat()->get_pointer();
  if (pointer) {
    event->button.device = pointer->gobj();
    gdk_event_set_device(event, pointer->gobj());
    gdk_event_set_source_device(event, pointer->gobj());
  }
  const bool handled = gtk_widget_event(widget.gobj(), event) != FALSE;
  gdk_event_free(event);
  return handled;
}

void click_face(Gtk::Widget& widget, guint32 time)
{
  press_face(widget, time, GDK_BUTTON_PRESS);
  press_face(widget, time, GDK_BUTTON_RELEASE);
}

writeit::Document paint_source()
{
  writeit::Document doc = writeit::blank_document("Sans", 11);
  doc.paragraphs[0].runs = {rich_run("Source")};
  doc.paragraphs[0].align = writeit::Align::Center;
  doc.paragraphs[0].direct = writeit::kDirectAlign;
  doc.paragraphs.push_back(paragraph_of("Target"));
  return doc;
}

writeit::Document paint_table()
{
  writeit::Document doc = writeit::blank_document("Sans", 11);
  writeit::insert_table(doc.paragraphs, 1, 2, 2, 4800);
  writeit::Paragraph* source = cell_at(doc, 0, 0, 0);
  writeit::Paragraph* target = cell_at(doc, 0, 1, 0);
  if (source) {
    source->runs = {rich_run("Red")};
    source->cell.shading = solid(writeit::Ink::Red);
  }
  if (target)
    target->runs = {text_run("Plain")};
  return doc;
}

}  // namespace

constexpr int kChecks = 125;

int main(int argc, char* argv[])
{
  using writeit::MainWindowProbe;
  using writeit::PaintSpan;
  using writeit::PainterFormat;

  {
    writeit::Document doc = writeit::blank_document("Sans", 11);
    PainterFormat format;
    format.from_cell = true;
    CHECK(!writeit::sample_painter(doc.paragraphs, 1, 0, format));
    CHECK(format.from_cell);
    doc.paragraphs[0].mark = rich_run("");
    doc.paragraphs[0].basis = 22;
    doc.paragraphs[0].page_break = true;
    CHECK(writeit::sample_painter(doc.paragraphs, 0, 0, format));
    CHECK(rich_look(format.character) && format.character.text.empty());
    CHECK(format.paragraph.runs.empty() && !format.paragraph.mark && format.paragraph.basis == 0);
    CHECK(format.paragraph.page_break && !format.from_cell);

    writeit::Paragraph mixed;
    mixed.runs = {rich_run("R"), text_run("est")};
    mixed.basis = 9;
    doc.paragraphs = {mixed};
    CHECK(writeit::sample_painter(doc.paragraphs, 0, 0, format));
    CHECK(rich_look(format.character) && format.character.text.empty());
    CHECK(writeit::sample_painter(doc.paragraphs, 0, 2, format));
    CHECK(format.character.font == "Sans" && !format.character.bold &&
          format.character.text.empty());
    CHECK(writeit::sample_painter(doc.paragraphs, 0, 99, format));
    CHECK(format.character.font == "Sans" && !format.character.bold);
    mixed.runs = {text_run("ab"), rich_run("Z")};
    doc.paragraphs = {mixed};
    CHECK(writeit::sample_painter(doc.paragraphs, 0, 99, format));
    CHECK(rich_look(format.character) && format.character.text.empty());
    CHECK(format.paragraph.basis == 0);
  }

  {
    writeit::Document doc = hello_world();
    std::vector<PaintSpan> spans;
    CHECK(writeit::painter_body_spans(doc.paragraphs, 0, 4, spans));
    CHECK(spans.size() == 1 && span_is(spans, 0, 0, 0, 4));
    CHECK(writeit::painter_body_spans(doc.paragraphs, 6, 11, spans));
    CHECK(spans.size() == 1 && span_is(spans, 0, 1, 0, 5));
    CHECK(writeit::painter_body_spans(doc.paragraphs, 0, 11, spans));
    CHECK(spans.size() == 2 && span_is(spans, 0, 0, 0, 5) && span_is(spans, 1, 1, 0, 5));
    CHECK(writeit::painter_body_spans(doc.paragraphs, 11, 6, spans));
    CHECK(spans.size() == 1 && span_is(spans, 0, 1, 0, 5));
    CHECK(writeit::painter_body_spans(doc.paragraphs, 5, 5, spans));
    CHECK(spans.size() == 1 && span_is(spans, 0, 0, 0, 0));
    CHECK(writeit::painter_body_spans(doc.paragraphs, 6, 6, spans));
    CHECK(spans.size() == 1 && span_is(spans, 0, 1, 0, 0));
    CHECK(writeit::painter_body_spans(doc.paragraphs, 4, 7, spans));
    CHECK(spans.size() == 2 && span_is(spans, 0, 0, 4, 5) && span_is(spans, 1, 1, 0, 1));

    CHECK(writeit::insert_table(doc.paragraphs, 1, 1, 1, 4800));
    CHECK(writeit::painter_body_spans(doc.paragraphs, 0, 13, spans));
    CHECK(spans.size() == 2 && span_is(spans, 0, 0, 0, 5) && span_is(spans, 1, 2, 0, 5));
    CHECK(spans.size() == 2 && doc.paragraphs[spans[0].index].cell.table == 0 &&
          doc.paragraphs[spans[1].index].cell.table == 0);
    CHECK(!writeit::painter_body_spans(doc.paragraphs, 6, 6, spans));
    CHECK(spans.empty());

    std::vector<writeit::Paragraph> lines = {paragraph_of("ab"), paragraph_of("cd")};
    CHECK(writeit::painter_linear_spans(lines, 4, 3, 5, spans));
    CHECK(spans.size() == 1 && span_is(spans, 0, 5, 0, 2));
    CHECK(writeit::painter_linear_spans(lines, 4, 5, 3, spans));
    CHECK(spans.size() == 1 && span_is(spans, 0, 5, 0, 2));
    CHECK(writeit::painter_linear_spans(lines, 4, 2, 2, spans));
    CHECK(spans.size() == 1 && span_is(spans, 0, 4, 0, 0));
    std::vector<writeit::Paragraph> none;
    CHECK(!writeit::painter_linear_spans(none, 0, 0, 1, spans));
    CHECK(!writeit::painter_body_spans(none, 0, 1, spans));
  }

  {
    writeit::Document doc;
    doc.paragraphs = {paragraph_of("Worldly")};
    const writeit::Document original = doc;
    PainterFormat format = brush();
    CHECK(!writeit::paint_spans(doc.paragraphs, {}, format));
    CHECK(!writeit::paint_spans(doc.paragraphs, {PaintSpan{9, 0, 1}}, format));
    CHECK(!writeit::paint_spans(doc.paragraphs, {PaintSpan{0, 0, 1}, PaintSpan{9, 0, 1}}, format));
    CHECK(doc.paragraphs == original.paragraphs);

    CHECK(writeit::paint_spans(doc.paragraphs, {PaintSpan{0, 0, 3}}, format));
    CHECK(doc.paragraphs[0].runs.size() == 2 && doc.paragraphs[0].runs[0].text == "Wor" &&
          rich_look(doc.paragraphs[0].runs[0]));
    CHECK(doc.paragraphs[0].runs[1].text == "ldly" && doc.paragraphs[0].runs[1].font == "Sans" &&
          !doc.paragraphs[0].runs[1].bold);
    CHECK(dressed(doc.paragraphs[0]) && !doc.paragraphs[0].page_break);

    doc.paragraphs = {paragraph_of("Worldly")};
    CHECK(writeit::paint_spans(doc.paragraphs, {PaintSpan{0, 5, 1}}, format));
    CHECK(doc.paragraphs[0].runs.size() == 3 && doc.paragraphs[0].runs[0].text == "W" &&
          !doc.paragraphs[0].runs[0].bold && doc.paragraphs[0].runs[1].text == "orld" &&
          rich_look(doc.paragraphs[0].runs[1]) && doc.paragraphs[0].runs[2].text == "ly" &&
          !doc.paragraphs[0].runs[2].bold);

    doc.paragraphs = {paragraph_of("héllo")};
    CHECK(writeit::paint_spans(doc.paragraphs, {PaintSpan{0, 0, 2}}, format));
    CHECK(doc.paragraphs[0].runs.size() == 2 && doc.paragraphs[0].runs[0].text == "hé" &&
          rich_look(doc.paragraphs[0].runs[0]) && doc.paragraphs[0].runs[1].text == "llo" &&
          !doc.paragraphs[0].runs[1].bold);

    writeit::Paragraph split;
    writeit::Run gap;
    split.runs = {gap, text_run("Ab")};
    doc.paragraphs = {split};
    CHECK(writeit::paint_spans(doc.paragraphs, {PaintSpan{0, 0, 1}}, format));
    CHECK(doc.paragraphs[0].runs.size() == 3 && doc.paragraphs[0].runs[0].text.empty() &&
          doc.paragraphs[0].runs[1].text == "A" && rich_look(doc.paragraphs[0].runs[1]) &&
          doc.paragraphs[0].runs[2].text == "b" && !doc.paragraphs[0].runs[2].bold);
  }

  {
    writeit::Document doc = hello_world();
    doc.paragraphs[0].page_break = true;
    doc.paragraphs[1].page_break = true;
    PainterFormat format = brush();
    format.paragraph.page_break = false;
    CHECK(writeit::paint_spans(doc.paragraphs, {PaintSpan{0, 0, 2}}, format));
    CHECK(dressed(doc.paragraphs[0]) && doc.paragraphs[0].page_break);
    CHECK(doc.paragraphs[0].runs.size() == 2 && doc.paragraphs[0].runs[0].text == "He" &&
          rich_look(doc.paragraphs[0].runs[0]) && doc.paragraphs[0].runs[1].text == "llo" &&
          doc.paragraphs[0].runs[1].font == "Sans");
    CHECK(plain_look(doc.paragraphs[1]) && doc.paragraphs[1].page_break &&
          joined(doc.paragraphs[1]) == "World" && !doc.paragraphs[1].runs[0].bold);
    const writeit::Document painted = doc;
    CHECK(!writeit::paint_spans(doc.paragraphs, {PaintSpan{0, 0, 2}}, format));
    CHECK(doc.paragraphs == painted.paragraphs);

    writeit::Paragraph empty;
    empty.page_break = true;
    doc.paragraphs = {empty};
    CHECK(writeit::paint_spans(doc.paragraphs, {PaintSpan{0, 0, 0}}, format));
    CHECK(dressed(doc.paragraphs[0]) && doc.paragraphs[0].page_break &&
          doc.paragraphs[0].runs.empty() && doc.paragraphs[0].mark &&
          rich_look(*doc.paragraphs[0].mark) && doc.paragraphs[0].mark->text.empty());
    CHECK(!writeit::paint_spans(doc.paragraphs, {PaintSpan{0, 0, 0}}, format));
  }

  {
    writeit::Paragraph paragraph;
    paragraph.runs = {text_run("A")};
    writeit::Run picture;
    writeit::Image image;
    image.type = "png";
    image.data = "x";
    picture.image = image;
    paragraph.runs.push_back(picture);
    paragraph.runs.push_back(text_run("B"));
    writeit::Run note = text_run("1");
    note.note = 1;
    paragraph.runs.push_back(note);
    writeit::Document doc;
    doc.paragraphs = {paragraph};
    PainterFormat format = brush();
    CHECK(writeit::paint_spans(doc.paragraphs, {PaintSpan{0, 0, 4}}, format));
    CHECK(doc.paragraphs[0].runs.size() == 4);
    CHECK(doc.paragraphs[0].runs[0].text == "A" && rich_look(doc.paragraphs[0].runs[0]));
    CHECK(doc.paragraphs[0].runs[1].image && doc.paragraphs[0].runs[1].image->data == "x" &&
          doc.paragraphs[0].runs[1].note == 0 && !doc.paragraphs[0].runs[1].bold);
    CHECK(doc.paragraphs[0].runs[2].text == "B" && rich_look(doc.paragraphs[0].runs[2]));
    CHECK(doc.paragraphs[0].runs[3].note == 1 && doc.paragraphs[0].runs[3].text == "1" &&
          !doc.paragraphs[0].runs[3].bold && doc.paragraphs[0].runs[3].font == "Sans");
  }

  {
    writeit::Document doc = writeit::blank_document("Sans", 11);
    CHECK(writeit::insert_table(doc.paragraphs, 1, 2, 2, 4800));
    writeit::Paragraph* corner = cell_at(doc, 0, 1, 0);
    CHECK(corner != nullptr);
    if (corner) {
      writeit::Paragraph extra = *corner;
      extra.runs = {text_run("more")};
      const auto at = static_cast<std::ptrdiff_t>(corner - doc.paragraphs.data()) + 1;
      doc.paragraphs.insert(doc.paragraphs.begin() + at, extra);
    }
    writeit::Paragraph* left = cell_at(doc, 0, 0, 0);
    writeit::Paragraph* right = cell_at(doc, 0, 1, 0);
    writeit::Paragraph* right_more = cell_at(doc, 0, 1, 1);
    writeit::Paragraph* below = cell_at(doc, 1, 0, 0);
    CHECK(left && right && right_more && below);
    if (left) {
      left->runs = {text_run("Keep")};
      left->cell.shading = solid(writeit::Ink::Blue);
      left->cell.borders_set = true;
      left->cell.borders.top.style = writeit::BorderStyle::Single;
      left->cell.borders.top.width = 10;
    }
    if (right)
      right->runs = {text_run("Go")};
    PainterFormat format = brush();
    format.from_cell = true;
    format.cell_shading = solid(writeit::Ink::Red);
    const size_t right_index = right ? static_cast<size_t>(right - doc.paragraphs.data()) : 0;
    CHECK(right && writeit::paint_spans(doc.paragraphs, {PaintSpan{right_index, 0, 1}}, format));
    right = cell_at(doc, 0, 1, 0);
    right_more = cell_at(doc, 0, 1, 1);
    left = cell_at(doc, 0, 0, 0);
    below = cell_at(doc, 1, 0, 0);
    const writeit::Paragraph* other = cell_at(doc, 1, 1, 0);
    CHECK(right && dressed(*right) && right->runs.size() == 2 && right->runs[0].text == "G" &&
          rich_look(right->runs[0]) && right->runs[1].text == "o" && !right->runs[1].bold);
    CHECK(right && filled(right->cell.shading, writeit::Ink::Red) && !right->cell.borders_set);
    CHECK(right_more && joined(*right_more) == "more" && plain_look(*right_more) &&
          filled(right_more->cell.shading, writeit::Ink::Red) && !right_more->cell.borders_set);
    CHECK(left && joined(*left) == "Keep" && filled(left->cell.shading, writeit::Ink::Blue) &&
          left->cell.borders_set && left->cell.borders.top.style == writeit::BorderStyle::Single);
    CHECK(below && writeit::shading_plain(below->cell.shading) && other &&
          writeit::shading_plain(other->cell.shading));

    PainterFormat body_brush = brush();
    body_brush.from_cell = false;
    body_brush.cell_shading = writeit::Shading{};
    const size_t left_index = left ? static_cast<size_t>(left - doc.paragraphs.data()) : 0;
    CHECK(left && writeit::paint_spans(doc.paragraphs, {PaintSpan{left_index, 0, 1}}, body_brush));
    left = cell_at(doc, 0, 0, 0);
    CHECK(left && filled(left->cell.shading, writeit::Ink::Blue) && left->cell.borders_set);

    doc.paragraphs[0].runs = {text_run("Body")};
    doc.paragraphs[0].page_break = true;
    PainterFormat cell_brush = brush();
    cell_brush.from_cell = true;
    cell_brush.cell_shading = solid(writeit::Ink::Red);
    CHECK(writeit::paint_spans(doc.paragraphs, {PaintSpan{0, 0, 2}}, cell_brush));
    CHECK(dressed(doc.paragraphs[0]) && doc.paragraphs[0].page_break &&
          doc.paragraphs[0].cell.table == 0 && writeit::shading_plain(doc.paragraphs[0].cell.shading));
    CHECK(!doc.paragraphs[0].runs.empty() && doc.paragraphs[0].runs[0].text == "Bo" &&
          rich_look(doc.paragraphs[0].runs[0]));

    PainterFormat clear = brush();
    clear.from_cell = true;
    clear.cell_shading = writeit::Shading{};
    right = cell_at(doc, 0, 1, 0);
    const size_t clear_index = right ? static_cast<size_t>(right - doc.paragraphs.data()) : 0;
    CHECK(right && writeit::paint_spans(doc.paragraphs, {PaintSpan{clear_index, 0, 1}}, clear));
    right = cell_at(doc, 0, 1, 0);
    right_more = cell_at(doc, 0, 1, 1);
    left = cell_at(doc, 0, 0, 0);
    CHECK(right && writeit::shading_plain(right->cell.shading));
    CHECK(right_more && writeit::shading_plain(right_more->cell.shading));
    CHECK(left && filled(left->cell.shading, writeit::Ink::Blue));
  }

  {
    writeit::Document doc = writeit::blank_document("Sans", 11);
    doc.paragraphs[0].runs = {rich_run("Painted")};
    doc.paragraphs[0].spacing.before = 200;
    doc.paragraphs[0].spacing.after = 100;
    doc.paragraphs[0].spacing.rule = writeit::LineRule::Exactly;
    doc.paragraphs[0].spacing.value = 360;
    writeit::TabStop tab;
    tab.position = 1440;
    tab.align = writeit::TabAlign::Center;
    tab.leader = writeit::TabLeader::Dot;
    doc.paragraphs[0].tabs = {tab};
    doc.paragraphs[0].shading.pattern = 10000;
    doc.paragraphs[0].shading.background = writeit::Ink::Yellow;
    CHECK(writeit::insert_table(doc.paragraphs, 1, 1, 1, 4800));
    writeit::Paragraph* cell = cell_at(doc, 0, 0, 0);
    CHECK(cell != nullptr);
    if (cell) {
      cell->runs = {text_run("Cell")};
      cell->cell.shading = solid(writeit::Ink::Red);
    }
    writeit::Document back;
    CHECK(writeit::rtf_import(writeit::rtf_export(doc), back));
    const writeit::Paragraph* saved = body_named(back, "Painted");
    const writeit::Paragraph* saved_cell = cell_at(back, 0, 0, 0);
    CHECK(saved && saved->runs.size() == 1 && saved->runs[0].font == "Times" &&
          saved->runs[0].size == 18 &&
          saved->runs[0].bold && saved->runs[0].italic && saved->runs[0].underline &&
          saved->runs[0].color == writeit::Ink::Red &&
          saved->runs[0].highlight == writeit::Ink::Yellow && saved->runs[0].strike &&
          saved->runs[0].script == writeit::Script::Super && saved->runs[0].text == "Painted");
    CHECK(saved && saved->spacing.before == 200 && saved->spacing.after == 100 &&
          saved->spacing.rule == writeit::LineRule::Exactly && saved->spacing.value == 360);
    CHECK(saved && saved->tabs.size() == 1 && saved->tabs[0].position == 1440 &&
          saved->tabs[0].align == writeit::TabAlign::Center &&
          saved->tabs[0].leader == writeit::TabLeader::Dot);
    CHECK(saved && saved->shading.pattern == 10000 &&
          saved->shading.background == writeit::Ink::Yellow);
    CHECK(saved_cell && filled(saved_cell->cell.shading, writeit::Ink::Red) &&
          !saved_cell->cell.borders_set && joined(*saved_cell) == "Cell");
  }

  std::string home = Glib::build_filename(Glib::get_tmp_dir(), "write-it-painter-XXXXXX");
  if (!g_mkdtemp(&home[0]))
    return EXIT_FAILURE;
  g_setenv("XDG_CONFIG_HOME", home.c_str(), TRUE);
  g_setenv("GDK_BACKEND", "x11", FALSE);
  if (!gtk_init_check(&argc, &argv)) {
    if (suite_test::failures)
      return EXIT_FAILURE;
    std::cout << "painter: no display, skipped\n";
    return 77;
  }
  Gtk::Main kit(argc, argv);
  {
    writeit::MainWindow window;
    window.resize(800, 600);
    window.show();
    settle();
    Gtk::ToggleToolButton* painter = MainWindowProbe::painter(window);
    Gtk::Widget* face = MainWindowProbe::painter_face(window);
    CHECK(painter && face && face->get_window());
    const auto tip = painter ? painter->get_tooltip_text() : "";
    const auto face_tip = face ? face->get_tooltip_text() : "";
    CHECK(painter && !painter->get_active() && (tip == "Format Painter" || face_tip == "Format Painter"));
    CHECK(painter && (painter->get_icon_name() == "draw-brush" || painter->get_label() == "Painter"));
    if (face)
      click_face(*face, 1000);
    settle();
    CHECK(MainWindowProbe::painter_on(window) && painter && painter->get_active() &&
          !MainWindowProbe::painter_sticky(window));
    if (face)
      click_face(*face, 1100);
    settle();
    CHECK(MainWindowProbe::painter_sticky(window) && painter && painter->get_active());
    CHECK(MainWindowProbe::escape(window));
    CHECK(!MainWindowProbe::painter_on(window) && painter && !painter->get_active() &&
          !MainWindowProbe::painter_sticky(window));

    MainWindowProbe::load(window, paint_source());
    settle();
    Gtk::TextView& body = MainWindowProbe::body(window);
    CHECK(body.get_buffer() && body.get_buffer()->get_char_count() == 13);
    MainWindowProbe::select(body, 0, 6);
    if (face)
      click_face(*face, 3000);
    settle();
    CHECK(MainWindowProbe::painter_on(window) && !MainWindowProbe::painter_sticky(window));
    MainWindowProbe::select(body, 7, 13);
    MainWindowProbe::queue(window, body);
    settle();
    writeit::Document painted = MainWindowProbe::doc(window);
    const writeit::Paragraph* source = body_named(painted, "Source");
    const writeit::Paragraph* target = body_named(painted, "Target");
    CHECK(source && source->align == writeit::Align::Center && source->runs.size() == 1 &&
          rich_look(source->runs[0]) && source->runs[0].text == "Source");
    CHECK(target && target->align == writeit::Align::Center && target->runs.size() == 1 &&
          rich_look(target->runs[0]) && target->runs[0].text == "Target");
    CHECK(!MainWindowProbe::painter_on(window) && MainWindowProbe::can_undo(window));
    MainWindowProbe::undo(window);
    settle();
    writeit::Document restored = MainWindowProbe::doc(window);
    target = body_named(restored, "Target");
    CHECK(target && target->align == writeit::Align::Left && target->runs.size() == 1 &&
          target->runs[0].font == "Sans" && !target->runs[0].bold && !MainWindowProbe::can_undo(window));

    MainWindowProbe::select(body, 0, 6);
    if (face)
      click_face(*face, 4000);
    settle();
    MainWindowProbe::apply(window, body);
    settle();
    CHECK(!MainWindowProbe::can_undo(window) && !MainWindowProbe::painter_on(window));

    MainWindowProbe::select(body, 0, 6);
    if (face) {
      click_face(*face, 5000);
      click_face(*face, 5100);
    }
    settle();
    CHECK(MainWindowProbe::painter_on(window) && MainWindowProbe::painter_sticky(window));
    MainWindowProbe::select(body, 7, 13);
    MainWindowProbe::apply(window, body);
    settle();
    painted = MainWindowProbe::doc(window);
    target = body_named(painted, "Target");
    CHECK(target && target->runs.size() == 1 && rich_look(target->runs[0]) &&
          MainWindowProbe::painter_on(window) && MainWindowProbe::painter_sticky(window));
    MainWindowProbe::undo(window);
    settle();
    CHECK(MainWindowProbe::painter_on(window));
    MainWindowProbe::caret(body, 10);
    MainWindowProbe::apply(window, body);
    settle();
    painted = MainWindowProbe::doc(window);
    target = body_named(painted, "Target");
    CHECK(target && target->runs.size() == 1 && rich_look(target->runs[0]) &&
          target->align == writeit::Align::Center && MainWindowProbe::painter_sticky(window));
    CHECK(MainWindowProbe::escape(window));
    CHECK(!MainWindowProbe::painter_on(window) && painter && !painter->get_active());

    MainWindowProbe::load(window, paint_table());
    settle();
    Gtk::TextView* from = MainWindowProbe::cell_view(window, 0, 0);
    Gtk::TextView* onto = MainWindowProbe::cell_view(window, 0, 1);
    CHECK(from && onto && from->get_buffer() && from->get_buffer()->get_char_count() == 3);
    MainWindowProbe::focus_cell(window, 0, 0);
    settle();
    if (from)
      MainWindowProbe::select(*from, 0, 3);
    if (face)
      click_face(*face, 7000);
    settle();
    CHECK(MainWindowProbe::painter_on(window));
    MainWindowProbe::focus_cell(window, 0, 1);
    settle();
    if (onto)
      MainWindowProbe::select(*onto, 0, 5);
    if (onto)
      MainWindowProbe::apply(window, *onto);
    settle();
    writeit::Document cells = MainWindowProbe::doc(window);
    const writeit::Paragraph* red = cell_at(cells, 0, 0, 0);
    const writeit::Paragraph* plain = cell_at(cells, 0, 1, 0);
    const writeit::Paragraph* neighbour = cell_at(cells, 1, 0, 0);
    CHECK(plain && filled(plain->cell.shading, writeit::Ink::Red) && !plain->cell.borders_set &&
          plain->runs.size() == 1 && rich_look(plain->runs[0]) && plain->runs[0].text == "Plain");
    CHECK(red && filled(red->cell.shading, writeit::Ink::Red) && joined(*red) == "Red");
    CHECK(neighbour && writeit::shading_plain(neighbour->cell.shading) &&
          !neighbour->cell.borders_set && !MainWindowProbe::painter_on(window));
    CHECK(MainWindowProbe::can_undo(window));
    MainWindowProbe::undo(window);
    settle();
    cells = MainWindowProbe::doc(window);
    plain = cell_at(cells, 0, 1, 0);
    CHECK(plain && writeit::shading_plain(plain->cell.shading) && joined(*plain) == "Plain" &&
          plain->runs.size() == 1 && !plain->runs[0].bold);
  }
  g_rmdir(home.c_str());
  return suite_test::done("painter", kChecks);
}
