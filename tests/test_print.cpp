/* SPDX-License-Identifier: Unlicense */

// Print draws the page view onto the document's paper. No system dialog:
// the same drawing is written to a PDF. CI runs this under xvfb.

#include "check.hpp"
#include "main_window.hpp"

#include <glib.h>
#include <glib/gstdio.h>
#include <glibmm/fileutils.h>
#include <glibmm/miscutils.h>
#include <gtkmm.h>

#include <zlib.h>

#include <cstdio>
#include <cstdlib>
#include <string>

namespace writeit {

struct MainWindowProbe {
  static void set_zoom(MainWindow& window, int zoom)
  {
    window.set_zoom(zoom);
  }
  static void set_view(MainWindow& window, ViewMode mode)
  {
    window.set_view(mode);
  }
  static ViewMode view(MainWindow& window)
  {
    return window.view_;
  }
  static void set_paper(MainWindow& window, int width, int height)
  {
    window.page_setup_.paper_width = width;
    window.page_setup_.paper_height = height;
    window.apply_page_size();
  }
  static Glib::RefPtr<Gtk::TextBuffer> buffer(MainWindow& window)
  {
    return window.buffer_;
  }
  static bool print_on(MainWindow& window)
  {
    return window.print_item_ && window.print_item_->get_sensitive() && window.print_tool_ &&
           window.print_tool_->get_sensitive();
  }
  static bool export_page(MainWindow& window, const std::string& path)
  {
    return window.export_page(path);
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

// Cairo writes the page tree and the text into compressed streams.
std::string inflate_pdf(const std::string& pdf)
{
  std::string out;
  size_t i = 0;
  while ((i = pdf.find("stream\n", i)) != std::string::npos) {
    i += 7;
    const size_t end = pdf.find("endstream", i);
    if (end == std::string::npos)
      break;
    size_t data_end = end;
    if (data_end > i && pdf[data_end - 1] == '\n')
      --data_end;
    std::string chunk = pdf.substr(i, data_end - i);
    z_stream strm{};
    strm.next_in = reinterpret_cast<Bytef*>(chunk.data());
    strm.avail_in = static_cast<uInt>(chunk.size());
    if (inflateInit(&strm) == Z_OK) {
      std::string buf(chunk.size() * 8 + 64, '\0');
      int ret = Z_OK;
      while (ret == Z_OK) {
        if (strm.total_out >= buf.size())
          buf.resize(buf.size() * 2);
        strm.next_out = reinterpret_cast<Bytef*>(&buf[strm.total_out]);
        strm.avail_out = static_cast<uInt>(buf.size() - strm.total_out);
        ret = inflate(&strm, Z_NO_FLUSH);
      }
      if (ret == Z_STREAM_END)
        out.append(buf.data(), strm.total_out);
      inflateEnd(&strm);
    }
    i = end + 9;
  }
  return out;
}

bool media_box(const std::string& pdf, double& width, double& height)
{
  const std::string plain = inflate_pdf(pdf);
  const size_t at = plain.find("/MediaBox");
  if (at == std::string::npos)
    return false;
  const size_t bracket = plain.find('[', at);
  if (bracket == std::string::npos || bracket + 1 >= plain.size())
    return false;
  double x = 0;
  double y = 0;
  return std::sscanf(plain.c_str() + bracket + 1, "%lf %lf %lf %lf", &x, &y, &width, &height) == 4;
}

int page_count(const std::string& pdf)
{
  const std::string plain = inflate_pdf(pdf);
  const size_t at = plain.find("/Type /Pages");
  if (at == std::string::npos)
    return -1;
  const size_t count = plain.find("/Count ", at);
  if (count == std::string::npos)
    return -1;
  return std::atoi(plain.c_str() + count + 7);
}

bool near(double value, double want)
{
  return value > want - 2.0 && value < want + 2.0;
}

std::string many_lines()
{
  std::string text;
  for (int i = 0; i < 80; ++i)
    text += "Line " + std::to_string(i) + "\n";
  return text;
}

}  // namespace

constexpr int kChecks = 23;

int main(int argc, char* argv[])
{
  std::string home = Glib::build_filename(Glib::get_tmp_dir(), "write-it-print-XXXXXX");
  if (!g_mkdtemp(&home[0]))
    return EXIT_FAILURE;
  g_setenv("XDG_CONFIG_HOME", home.c_str(), TRUE);
  g_setenv("GDK_BACKEND", "x11", FALSE);
  if (!gtk_init_check(&argc, &argv)) {
    std::cout << "print: no display, skipped\n";
    return 77;
  }
  Gtk::Main kit(argc, argv);
  {
    writeit::MainWindow window;
    window.show();
    settle();
    CHECK(MainWindowProbe::print_on(window));
    MainWindowProbe::set_zoom(window, 100);
    settle();

    const std::string a4 = Glib::build_filename(home, "a4.pdf");
    MainWindowProbe::buffer(window)->set_text("Hello from Write-It.");
    settle();
    CHECK(MainWindowProbe::export_page(window, a4));
    const std::string pdf = Glib::file_get_contents(a4);
    CHECK(!pdf.empty());
    CHECK(pdf.compare(0, 5, "%PDF-") == 0);
    CHECK(page_count(pdf) == 1);
    double width = 0;
    double height = 0;
    CHECK(media_box(pdf, width, height));
    // Default paper is A4: 11906 by 16838 twips, 20 twips to the point.
    CHECK(near(width, 11906.0 / 20.0));
    CHECK(near(height, 16838.0 / 20.0));
    // The words are drawn as text. Cairo splits them for kerning, so the
    // page holds a text show rather than the literal "Hello".
    CHECK(inflate_pdf(pdf).find("]TJ") != std::string::npos);

    MainWindowProbe::set_view(window, writeit::ViewMode::Draft);
    settle();
    const std::string draft = Glib::build_filename(home, "draft.pdf");
    CHECK(MainWindowProbe::export_page(window, draft));
    CHECK(MainWindowProbe::view(window) == writeit::ViewMode::Draft);
    const std::string draft_pdf = Glib::file_get_contents(draft);
    CHECK(!draft_pdf.empty());
    CHECK(page_count(draft_pdf) == 1);
    CHECK(media_box(draft_pdf, width, height));
    CHECK(near(width, 11906.0 / 20.0));

    MainWindowProbe::set_view(window, writeit::ViewMode::Page);
    // US Letter, 8.5 by 11 inches, already the edges that print.
    MainWindowProbe::set_paper(window, 12240, 15840);
    settle();
    const std::string letter = Glib::build_filename(home, "letter.pdf");
    CHECK(MainWindowProbe::export_page(window, letter));
    const std::string letter_pdf = Glib::file_get_contents(letter);
    CHECK(!letter_pdf.empty());
    CHECK(media_box(letter_pdf, width, height));
    CHECK(near(width, 612));
    CHECK(near(height, 792));

    MainWindowProbe::buffer(window)->set_text(many_lines());
    settle();
    const std::string long_pdf_path = Glib::build_filename(home, "long.pdf");
    CHECK(MainWindowProbe::export_page(window, long_pdf_path));
    const std::string long_pdf = Glib::file_get_contents(long_pdf_path);
    CHECK(!long_pdf.empty());
    CHECK(page_count(long_pdf) >= 2);
  }
  return suite_test::done("print", kChecks);
}
