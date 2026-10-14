/* SPDX-License-Identifier: Unlicense */

// Zoom scales body text already. Pictures are pixbufs, and footnotes, the
// header, and the footer are separate views, so they have to be scaled too.
// The file bytes stay the picture that was inserted. Zoom adds no undo step.

#include "check.hpp"
#include "document.hpp"
#include "main_window.hpp"
#include "view.hpp"

#include <glib.h>
#include <glib/gstdio.h>
#include <glibmm/miscutils.h>
#include <gtkmm.h>

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

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

  static void zoom(MainWindow& window, int percent)
  {
    window.set_zoom(percent);
  }

  static Glib::RefPtr<Gtk::TextBuffer> buffer(MainWindow& window)
  {
    return window.buffer_;
  }

  static Gtk::TextView* cell(MainWindow& window)
  {
    return window.nth_cell_view(0, 0, 0);
  }

  static Gtk::TextView& notes(MainWindow& window)
  {
    return window.notes_view_;
  }

  static Gtk::TextView& header(MainWindow& window)
  {
    return window.header_view_;
  }

  static Gtk::TextView& footer(MainWindow& window)
  {
    return window.footer_view_;
  }

  static Gtk::Label& note_label(MainWindow& window)
  {
    return window.note_label_;
  }

  static Gtk::Fixed& note_marks(MainWindow& window)
  {
    return window.note_marks_;
  }

  static std::string page(MainWindow& window)
  {
    return window.page_label_.get_text().raw();
  }

  static std::size_t steps(MainWindow& window)
  {
    return window.undo_.size();
  }

  static long stray(MainWindow& window)
  {
    return window.undo_.stray_edits();
  }

  static std::uint64_t state(MainWindow& window)
  {
    return window.undo_.state_id();
  }

  static bool dirty(MainWindow& window)
  {
    return window.dirty();
  }

  static void undo(MainWindow& window)
  {
    window.undo();
  }

  static Document doc(MainWindow& window)
  {
    return window.capture();
  }
};

}  // namespace writeit

namespace {

using writeit::Document;
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

Glib::RefPtr<Gdk::Pixbuf> solid(int width, int height, guint32 fill)
{
  auto pix = Gdk::Pixbuf::create(Gdk::COLORSPACE_RGB, false, 8, width, height);
  if (pix)
    pix->fill(fill);
  return pix;
}

std::string png_of(const Glib::RefPtr<Gdk::Pixbuf>& pix)
{
  gchar* raw = nullptr;
  gsize size = 0;
  pix->save_to_buffer(raw, size, "png");
  std::string out(raw, raw + size);
  g_free(raw);
  return out;
}

struct Px {
  bool found = false;
  int w = 0;
  int h = 0;
};

Px first_pixbuf(const Glib::RefPtr<Gtk::TextBuffer>& buffer)
{
  Px out;
  if (!buffer)
    return out;
  for (auto iter = buffer->begin(); !iter.is_end(); ++iter) {
    if (auto pix = iter.get_pixbuf()) {
      out.found = true;
      out.w = pix->get_width();
      out.h = pix->get_height();
      return out;
    }
  }
  return out;
}

int font_px(Gtk::Widget& widget)
{
  int size = 0;
  if (auto context = widget.get_pango_context())
    size = context->get_font_description().get_size();
  if (size <= 0) {
    if (auto style = widget.get_style_context())
      size = style->get_font(style->get_state()).get_size();
  }
  return size;
}

bool font_is(Gtk::Widget& widget, int want)
{
  const int got = font_px(widget);
  if (got != want)
    std::cerr << "font " << got << " want " << want << "\n";
  return got == want;
}

const writeit::Image* image_at(const Document& doc, bool cell)
{
  for (const writeit::Paragraph& paragraph : doc.paragraphs) {
    if ((paragraph.cell.table != 0) != cell)
      continue;
    for (const writeit::Run& run : paragraph.runs) {
      if (run.image)
        return &*run.image;
    }
  }
  return nullptr;
}

Glib::RefPtr<Gtk::TextBuffer> cell_buffer(writeit::MainWindow& window)
{
  Gtk::TextView* cell = MainWindowProbe::cell(window);
  if (!cell)
    return {};
  return cell->get_buffer();
}

int story_px(int percent)
{
  return static_cast<int>(11 * (percent / 100.0) * PANGO_SCALE);
}

// 17 checks. A wide picture caps at 480 before the zoom; a small one does not.
void expect_zoom(writeit::MainWindow& window, int percent, int body_w, int body_h, int cell_w,
                 int cell_h, int margin, int mark)
{
  MainWindowProbe::zoom(window, percent);
  settle();
  const Px body = first_pixbuf(MainWindowProbe::buffer(window));
  const Px cell = first_pixbuf(cell_buffer(window));
  const int font = story_px(percent);
  const std::string label = MainWindowProbe::page(window);
  if (label != "Page 1 of 1")
    std::cerr << "page label \"" << label << "\"\n";
  CHECK(body.found);
  CHECK(body.w == body_w);
  CHECK(body.h == body_h);
  CHECK(cell.found);
  CHECK(cell.w == cell_w);
  CHECK(cell.h == cell_h);
  CHECK(font_is(MainWindowProbe::notes(window), font));
  CHECK(font_is(MainWindowProbe::header(window), font));
  CHECK(font_is(MainWindowProbe::footer(window), font));
  CHECK(font_is(MainWindowProbe::note_label(window), font));
  CHECK(MainWindowProbe::header(window).get_left_margin() == margin);
  CHECK(MainWindowProbe::notes(window).get_left_margin() == margin);
  CHECK(MainWindowProbe::note_marks(window).get_margin_right() == mark);
  CHECK(label == "Page 1 of 1");
  CHECK(MainWindowProbe::steps(window) == 0);
  CHECK(!MainWindowProbe::dirty(window));
  CHECK(MainWindowProbe::stray(window) == 0);
}

}  // namespace

// 3 setup + 17 * 4 zooms + 3 after the tour + 17 around deleting the picture.
constexpr int kChecks = 91;

int main(int argc, char* argv[])
{
  std::string home = Glib::build_filename(Glib::get_tmp_dir(), "write-it-zoom-scale-XXXXXX");
  if (!g_mkdtemp(&home[0]))
    return EXIT_FAILURE;
  g_setenv("XDG_CONFIG_HOME", home.c_str(), TRUE);
  g_setenv("GDK_BACKEND", "x11", FALSE);
  if (!gtk_init_check(&argc, &argv)) {
    std::cout << "zoom-scale: no display, skipped\n";
    return 77;
  }
  Gtk::Main kit(argc, argv);

  auto wide = solid(640, 320, 0x336699ff);
  auto small = solid(100, 50, 0xcc8844ff);
  CHECK(static_cast<bool>(wide));
  CHECK(static_cast<bool>(small));
  const std::string wide_png = png_of(wide);
  const std::string small_png = png_of(small);

  Document doc = writeit::blank_document("Sans", 11);
  writeit::Run picture;
  writeit::Image image;
  image.type = "png";
  image.alt = "wide";
  image.data = wide_png;
  picture.image = std::move(image);
  doc.paragraphs[0].runs.push_back(std::move(picture));
  writeit::Run marker;
  marker.font = "Sans";
  marker.size = 11;
  marker.text = "1";
  marker.note = 1;
  doc.paragraphs[0].runs.push_back(std::move(marker));

  writeit::Paragraph heading;
  writeit::Run heading_run;
  heading_run.text = "Header";
  heading.runs.push_back(std::move(heading_run));
  doc.header.push_back(std::move(heading));
  writeit::Paragraph footing;
  writeit::Run footing_run;
  footing_run.text = "Footer";
  footing.runs.push_back(std::move(footing_run));
  doc.footer.push_back(std::move(footing));
  writeit::Paragraph note;
  writeit::Run note_run;
  note_run.text = "Noted.";
  note.runs.push_back(std::move(note_run));
  doc.notes.push_back({std::move(note)});

  CHECK(writeit::insert_table(doc.paragraphs, doc.paragraphs.size(), 1, 1,
                              writeit::page_text_twips(doc.page)));
  for (writeit::Paragraph& paragraph : doc.paragraphs) {
    if (paragraph.cell.table == 0)
      continue;
    writeit::Run cell_picture;
    writeit::Image cell_image;
    cell_image.type = "png";
    cell_image.alt = "cell";
    cell_image.data = small_png;
    cell_picture.image = std::move(cell_image);
    paragraph.runs.push_back(std::move(cell_picture));
    break;
  }

  writeit::MainWindow window;
  window.show();
  MainWindowProbe::load(window, doc);
  const std::uint64_t saved = MainWindowProbe::state(window);

  expect_zoom(window, 100, 480, 240, 100, 50, 8, 6);
  expect_zoom(window, 75, 360, 180, 75, 38, 6, 5);
  expect_zoom(window, 50, 240, 120, 50, 25, 4, 3);
  expect_zoom(window, 100, 480, 240, 100, 50, 8, 6);

  CHECK(MainWindowProbe::state(window) == saved);
  const Document shown = MainWindowProbe::doc(window);
  const writeit::Image* body = image_at(shown, false);
  const writeit::Image* cell = image_at(shown, true);
  CHECK(body && body->data == wide_png);
  CHECK(cell && cell->data == small_png);

  int at = -1;
  auto buffer = MainWindowProbe::buffer(window);
  for (auto iter = buffer->begin(); !iter.is_end(); ++iter) {
    if (iter.get_pixbuf()) {
      at = iter.get_offset();
      break;
    }
  }
  CHECK(at >= 0);
  if (at >= 0) {
    buffer->begin_user_action();
    auto start = buffer->get_iter_at_offset(at);
    auto end = start;
    end.forward_char();
    buffer->erase(start, end);
    buffer->end_user_action();
  }
  CHECK(MainWindowProbe::steps(window) == 1);
  CHECK(MainWindowProbe::dirty(window));
  CHECK(MainWindowProbe::stray(window) == 0);

  MainWindowProbe::zoom(window, 50);
  settle();
  CHECK(MainWindowProbe::steps(window) == 1);
  CHECK(MainWindowProbe::stray(window) == 0);
  MainWindowProbe::undo(window);
  settle();
  const Px back = first_pixbuf(MainWindowProbe::buffer(window));
  const Px cell_back = first_pixbuf(cell_buffer(window));
  CHECK(back.found);
  CHECK(back.w == 240);
  CHECK(back.h == 120);
  CHECK(cell_back.w == 50);
  CHECK(cell_back.h == 25);
  const Document restored = MainWindowProbe::doc(window);
  const writeit::Image* body_back = image_at(restored, false);
  const writeit::Image* cell_kept = image_at(restored, true);
  CHECK(body_back && body_back->data == wide_png);
  CHECK(cell_kept && cell_kept->data == small_png);
  CHECK(!MainWindowProbe::dirty(window));
  CHECK(MainWindowProbe::state(window) == saved);
  CHECK(MainWindowProbe::stray(window) == 0);
  CHECK(MainWindowProbe::steps(window) == 0);

  return suite_test::done("zoom-scale", kChecks);
}
