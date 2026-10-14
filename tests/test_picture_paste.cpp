/* SPDX-License-Identifier: Unlicense */

// Ctrl+V leaves a pixbuf in the buffer that image_pix_ does not know.
// capture() (body) and read_cell() (a cell) must store PNG bytes. Bytes a
// picture already had are not replaced by a re-encode of the screen pixbuf.

#include "check.hpp"
#include "document.hpp"
#include "main_window.hpp"

#include <glib.h>
#include <glib/gstdio.h>
#include <glibmm/miscutils.h>
#include <gtkmm.h>

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

  static void paste_body(MainWindow& window, const Glib::RefPtr<Gdk::Pixbuf>& pix)
  {
    window.buffer_->insert_pixbuf(window.buffer_->end(), pix);
  }

  static Gtk::TextView* cell(MainWindow& window)
  {
    return window.nth_cell_view(0, 0, 0);
  }
};

}  // namespace writeit

namespace {

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

const writeit::Image* image_at(const writeit::Document& doc, bool cell, int which)
{
  int seen = 0;
  for (const writeit::Paragraph& paragraph : doc.paragraphs) {
    if ((paragraph.cell.table != 0) != cell)
      continue;
    for (const writeit::Run& run : paragraph.runs) {
      if (!run.image)
        continue;
      if (seen == which)
        return &*run.image;
      ++seen;
    }
  }
  return nullptr;
}

}  // namespace

constexpr int kChecks = 16;

int main(int argc, char* argv[])
{
  std::string home = Glib::build_filename(Glib::get_tmp_dir(), "write-it-picture-paste-XXXXXX");
  if (!g_mkdtemp(&home[0]))
    return EXIT_FAILURE;
  g_setenv("XDG_CONFIG_HOME", home.c_str(), TRUE);
  g_setenv("GDK_BACKEND", "x11", FALSE);
  if (!gtk_init_check(&argc, &argv)) {
    std::cout << "picture-paste: no display, skipped\n";
    return 77;
  }
  Gtk::Main kit(argc, argv);

  auto kept_pix = solid(4, 4, 0x112233ff);
  auto pasted = solid(5, 3, 0xaabbccff);
  auto cell_pasted = solid(3, 3, 0x445566ff);
  CHECK(static_cast<bool>(kept_pix));
  CHECK(static_cast<bool>(pasted));
  CHECK(static_cast<bool>(cell_pasted));
  const std::string original = png_of(kept_pix);

  writeit::Document doc = writeit::blank_document("Sans", 11);
  writeit::Run run;
  writeit::Image known;
  known.type = "png";
  known.alt = "kept";
  known.data = original;
  run.image = std::move(known);
  doc.paragraphs[0].runs.push_back(std::move(run));

  writeit::MainWindow window;
  window.show();
  writeit::MainWindowProbe::load(window, doc);
  // As a paste does: the buffer insert, not Insert > Picture.
  writeit::MainWindowProbe::paste_body(window, pasted);

  const writeit::Document body = writeit::MainWindowProbe::doc(window);
  const writeit::Image* kept = image_at(body, false, 0);
  const writeit::Image* paste = image_at(body, false, 1);
  CHECK(kept && kept->data == original);
  CHECK(kept && kept->type == "png");
  CHECK(kept && kept->alt == "kept");
  CHECK(paste && !paste->data.empty());
  CHECK(paste && paste->type == "png");
  CHECK(paste && writeit::image_type_of(paste->data) == "png");
  CHECK(paste && paste->data != original);

  writeit::Document table = writeit::blank_document("Sans", 11);
  CHECK(writeit::insert_table(table.paragraphs, 0, 1, 1, writeit::page_text_twips(table.page)));
  writeit::MainWindowProbe::load(window, table);
  Gtk::TextView* cell = writeit::MainWindowProbe::cell(window);
  CHECK(cell != nullptr);
  if (cell && cell->get_buffer())
    cell->get_buffer()->insert_pixbuf(cell->get_buffer()->end(), cell_pasted);

  const writeit::Document cells = writeit::MainWindowProbe::doc(window);
  const writeit::Image* in_cell = image_at(cells, true, 0);
  CHECK(in_cell && !in_cell->data.empty());
  CHECK(in_cell && in_cell->type == "png");
  CHECK(in_cell && writeit::image_type_of(in_cell->data) == "png");
  CHECK(in_cell && paste && in_cell->data != paste->data);

  return suite_test::done("picture-paste", kChecks);
}
