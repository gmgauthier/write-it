/* SPDX-License-Identifier: Unlicense */

// A reconstruction of the typing-speed gate in DEVELOPMENT.md, not the lost
// bench.sh. Release build. plain1000.rtf and list1000.rtf are 1000 paragraphs
// of one sentence. The caret sits at the end of paragraph 500. Fifty letter
// keys are typed there, three times. Each key is its own undo step, because
// a key that already costs more than a second cannot merge with the next.
// CPU is process CPU time. Wall time is beside it. After each key the pending
// main-loop work, including list layout, is drained with no sleep. That drain
// is the refresh. The lost script's timer is not in the tree.

#include "document.hpp"
#include "main_window.hpp"

#include <glib.h>
#include <glib/gstdio.h>
#include <glibmm/fileutils.h>
#include <glibmm/miscutils.h>
#include <gtkmm.h>

#include <unistd.h>

#include <algorithm>
#include <ctime>
#include <iostream>
#include <string>
#include <vector>

namespace writeit {

struct MainWindowProbe {
  static bool open(MainWindow& w, const std::string& path)
  {
    return w.open_path(path, MainWindow::OpenKind::Rtf);
  }
  static void place_paragraph(MainWindow& w, int paragraph)
  {
    const int line = paragraph - 1;
    auto iter = w.buffer_->get_iter_at_line(line);
    if (!iter.ends_line())
      iter.forward_to_line_end();
    w.buffer_->place_cursor(iter);
    w.text_.scroll_to(w.buffer_->get_insert());
  }
  static int lines(MainWindow& w)
  {
    return w.buffer_->get_line_count();
  }
  static void isolate(MainWindow& w)
  {
    w.last_typed_us_ = 0;
  }
  // Opening another file asks to save when the buffer is dirty. The bench
  // discards the typed keys instead of waiting on that dialog.
  static void discard(MainWindow& w)
  {
    w.save_point_ = true;
    w.saved_state_ = w.undo_.state_id();
  }
  static void type_letter(MainWindow& w, char letter)
  {
    const std::string text(1, letter);
    w.buffer_->insert_interactive_at_cursor(text, true);
  }
};

}  // namespace writeit

namespace {

constexpr int kParagraphs = 1000;
constexpr int kTarget = 500;
constexpr int kKeys = 50;
constexpr int kRuns = 3;

double cpu_ms()
{
  timespec ts{};
  clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
  return static_cast<double>(ts.tv_sec) * 1000.0 + static_cast<double>(ts.tv_nsec) / 1.0e6;
}

double wall_ms()
{
  timespec ts{};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<double>(ts.tv_sec) * 1000.0 + static_cast<double>(ts.tv_nsec) / 1.0e6;
}

void drain()
{
  auto context = Glib::MainContext::get_default();
  for (int i = 0; i < 100000 && context->pending(); ++i)
    context->iteration(false);
}

double median(std::vector<double> values)
{
  if (values.empty())
    return 0;
  std::sort(values.begin(), values.end());
  const size_t n = values.size();
  if (n % 2 == 1)
    return values[n / 2];
  return 0.5 * (values[n / 2 - 1] + values[n / 2]);
}

writeit::Document make_doc(bool list)
{
  writeit::Document doc;
  for (int i = 1; i <= kParagraphs; ++i) {
    writeit::Paragraph paragraph;
    if (list)
      paragraph.list = writeit::ListFormat(writeit::ListKind::Number, 0);
    writeit::Run run;
    run.text = "The quick brown fox jumps over the lazy dog. " + std::to_string(i);
    paragraph.runs.push_back(run);
    doc.paragraphs.push_back(std::move(paragraph));
  }
  return doc;
}

struct KeySample {
  double handler_cpu = 0;
  double refresh_cpu = 0;
  double handler_wall = 0;
  double refresh_wall = 0;
};

KeySample type_one(writeit::MainWindow& window, char letter, bool isolate)
{
  drain();
  if (isolate)
    writeit::MainWindowProbe::isolate(window);
  const double cpu0 = cpu_ms();
  const double wall0 = wall_ms();
  writeit::MainWindowProbe::type_letter(window, letter);
  const double cpu1 = cpu_ms();
  const double wall1 = wall_ms();
  drain();
  const double cpu2 = cpu_ms();
  const double wall2 = wall_ms();
  KeySample sample;
  sample.handler_cpu = cpu1 - cpu0;
  sample.refresh_cpu = cpu2 - cpu1;
  sample.handler_wall = wall1 - wall0;
  sample.refresh_wall = wall2 - wall1;
  return sample;
}

struct RunResult {
  double handler_cpu = 0;
  double total_cpu = 0;
  double total_wall = 0;
};

RunResult run_keys(writeit::MainWindow& window, bool isolate, const char* label, int run)
{
  std::vector<double> handler_cpu;
  std::vector<double> total_cpu;
  std::vector<double> total_wall;
  handler_cpu.reserve(kKeys);
  total_cpu.reserve(kKeys);
  total_wall.reserve(kKeys);
  for (int i = 0; i < kKeys; ++i) {
    const char letter = static_cast<char>('a' + (i % 26));
    const KeySample sample = type_one(window, letter, isolate);
    handler_cpu.push_back(sample.handler_cpu);
    total_cpu.push_back(sample.handler_cpu + sample.refresh_cpu);
    total_wall.push_back(sample.handler_wall + sample.refresh_wall);
  }
  RunResult result;
  result.handler_cpu = median(handler_cpu);
  result.total_cpu = median(total_cpu);
  result.total_wall = median(total_wall);
  std::cout << "  " << label << " run " << run << ": handler " << result.handler_cpu
            << " ms CPU, with refresh " << result.total_cpu << " ms CPU, " << result.total_wall
            << " ms wall (median of " << kKeys << ")\n"
            << std::flush;
  return result;
}

void bench_file(writeit::MainWindow& window, const std::string& path, const char* label)
{
  std::vector<double> totals;
  for (int run = 1; run <= kRuns; ++run) {
    writeit::MainWindowProbe::discard(window);
    const double load0 = wall_ms();
    if (!writeit::MainWindowProbe::open(window, path)) {
      std::cerr << label << ": open failed\n";
      return;
    }
    drain();
    const int lines = writeit::MainWindowProbe::lines(window);
    if (lines < kTarget) {
      std::cerr << label << ": " << lines << " lines, need paragraph " << kTarget << "\n";
      return;
    }
    writeit::MainWindowProbe::place_paragraph(window, kTarget);
    drain();
    std::cout << "  " << label << " load " << (wall_ms() - load0) << " ms wall, " << lines
              << " lines\n"
              << std::flush;
    totals.push_back(run_keys(window, true, label, run).total_cpu);
  }
  const double gate = median(totals);
  std::cout << label << " gate: " << gate << " ms CPU per key (median of " << kRuns
            << " run medians), refresh included\n"
            << std::flush;
  // One burst, undo steps allowed to merge. Not part of the gate number.
  writeit::MainWindowProbe::discard(window);
  if (!writeit::MainWindowProbe::open(window, path))
    return;
  drain();
  writeit::MainWindowProbe::place_paragraph(window, kTarget);
  drain();
  run_keys(window, false, (std::string(label) + " burst").c_str(), 1);
}

}  // namespace

int main(int argc, char* argv[])
{
  std::string home = Glib::build_filename(Glib::get_tmp_dir(), "write-it-bench-XXXXXX");
  if (!g_mkdtemp(&home[0]))
    return 1;
  g_setenv("XDG_CONFIG_HOME", home.c_str(), TRUE);
  g_setenv("GDK_BACKEND", "x11", FALSE);
  g_setenv("NO_AT_BRIDGE", "1", FALSE);
  if (!gtk_init_check(&argc, &argv)) {
    std::cerr << "bench: no display\n";
    return 1;
  }
  Gtk::Main kit(argc, argv);

  const std::string plain_path = Glib::build_filename(home, "plain1000.rtf");
  const std::string list_path = Glib::build_filename(home, "list1000.rtf");
  Glib::file_set_contents(plain_path, writeit::rtf_export(make_doc(false)));
  Glib::file_set_contents(list_path, writeit::rtf_export(make_doc(true)));

  char host[256] = {};
  gethostname(host, sizeof host);
  std::cout << "host " << host << "\n";
  std::cout << "plain1000.rtf and list1000.rtf, paragraph " << kTarget << ", " << kKeys
            << " keys, median of " << kRuns << "\n"
            << std::flush;

  {
    writeit::MainWindow window;
    window.show();
    drain();
    bench_file(window, plain_path, "plain1000");
    bench_file(window, list_path, "list1000");
    window.hide();
    drain();
  }

  g_remove(plain_path.c_str());
  g_remove(list_path.c_str());
  g_rmdir(home.c_str());
  return 0;
}
