/* SPDX-License-Identifier: Unlicense */

// Save As and Export file names. GTK's save chooser selects only the stem of
// the suggested "Untitled.rtf", so what the user types lands in front of the
// ".rtf" that is left: "zout.rtf" comes back as "zout.rtf.rtf".

#include "check.hpp"
#include "filename.hpp"

#include <set>
#include <string>
#include <vector>

namespace {

using writeit::markdown_file;
using writeit::resolve_save;
using writeit::rtf_file;

std::string rtf(const std::string& chosen)
{
  return writeit::save_name(chosen, rtf_file());
}

std::string md(const std::string& chosen)
{
  return writeit::save_name(chosen, markdown_file());
}

// Each case as typed into an empty name field, and as the field reads when
// typed over the selected "Untitled" of "Untitled.rtf".
void typed(const std::string& text, const std::string& expected)
{
  CHECK(rtf(text) == expected);
  CHECK(rtf(text + ".rtf") == expected);
}

void save_as_names()
{
  typed("zout.rtf", "zout.rtf");
  typed("zout.RTF", "zout.RTF");
  typed("zout.Rtf", "zout.Rtf");
  typed("zout", "zout.rtf");
  // Save writes RTF, so another document extension is replaced, not kept:
  // RTF in a file called .txt or .md would open as the wrong thing.
  typed("zout.txt", "zout.rtf");
  typed("zout.TXT", "zout.rtf");
  typed("zout.md", "zout.rtf");
  typed("zout.markdown", "zout.rtf");
  // A dot that is not a document extension is part of the name.
  typed("my.report", "my.report.rtf");
  typed("v1.2", "v1.2.rtf");
  typed("my.report.rtf", "my.report.rtf");
  typed("my.report.txt", "my.report.rtf");
  // Trailing dots.
  typed("zout.", "zout.rtf");
  typed("zout...", "zout.rtf");
  CHECK(rtf("zout.rtf.") == "zout.rtf");
  // Only an extension: there is no name, so it is Untitled.
  typed(".rtf", "Untitled.rtf");
  typed(".RTF", "Untitled.RTF");
  typed(".txt", "Untitled.rtf");
  CHECK(rtf(".") == "Untitled.rtf");
  CHECK(rtf("") == "Untitled.rtf");
  // A hidden file's name keeps its leading dot.
  typed(".notes", ".notes.rtf");
  // Repeats collapse.
  CHECK(rtf("zout.rtf.rtf.rtf") == "zout.rtf");
  CHECK(rtf("zout.txt.md.rtf") == "zout.rtf");
}

void directories()
{
  typed("/home/greg/docs/zout", "/home/greg/docs/zout.rtf");
  typed("/home/greg/docs/zout.rtf", "/home/greg/docs/zout.rtf");
  typed("/home/greg/docs/zout.RTF", "/home/greg/docs/zout.RTF");
  typed("/home/greg/docs/zout.txt", "/home/greg/docs/zout.rtf");
  // Dots and extensions in directory names are not the file's.
  typed("/home/greg/my.docs/zout", "/home/greg/my.docs/zout.rtf");
  typed("/tmp/old.rtf/zout", "/tmp/old.rtf/zout.rtf");
  typed("/tmp/notes.txt/zout.md", "/tmp/notes.txt/zout.rtf");
  typed("/tmp/a.b/my.report", "/tmp/a.b/my.report.rtf");
  typed("/home/greg/docs/.rtf", "/home/greg/docs/Untitled.rtf");
  typed("/home/greg/docs/zout.", "/home/greg/docs/zout.rtf");
  typed("relative/dir/zout", "relative/dir/zout.rtf");
  CHECK(rtf("/home/greg/docs/") == "/home/greg/docs/Untitled.rtf");
}

void export_names()
{
  CHECK(md("notes") == "notes.md");
  CHECK(md("notes.md") == "notes.md");
  CHECK(md("notes.md.md") == "notes.md");
  CHECK(md("notes.MD") == "notes.MD");
  // The Export filter lists .markdown too, so it is kept.
  CHECK(md("notes.markdown") == "notes.markdown");
  CHECK(md("notes.markdown.md") == "notes.markdown");
  // Export writes Markdown: an RTF or text extension is replaced.
  CHECK(md("notes.rtf") == "notes.md");
  CHECK(md("notes.rtf.md") == "notes.md");
  CHECK(md("notes.txt") == "notes.md");
  CHECK(md("my.report") == "my.report.md");
  CHECK(md("notes.") == "notes.md");
  CHECK(md(".md") == "Untitled.md");
  CHECK(md("/tmp/old.rtf/notes") == "/tmp/old.rtf/notes.md");
}

void idempotent()
{
  const std::vector<std::string> names = {"zout",     "zout.rtf.rtf", "zout.RTF", "zout.txt",
                                          "my.report", "zout.",        ".rtf",     "/tmp/a.b/c"};
  for (const auto& name : names) {
    CHECK(rtf(rtf(name)) == rtf(name));
    CHECK(md(md(name)) == md(name));
  }
}

void extensions()
{
  CHECK(writeit::extension_of("/tmp/letter.RTF") == ".rtf");
  CHECK(writeit::extension_of("/tmp/notes.markdown") == ".markdown");
  CHECK(writeit::extension_of("/tmp/old.rtf/readme").empty());
  CHECK(writeit::extension_of("/tmp/readme").empty());
}

// Overwrite confirmation runs on the final name: the file that would really
// be replaced, after any extension is added or collapsed.
struct Disk {
  std::set<std::string> files;
  std::set<std::string> folders;
  std::vector<std::string> checked;
  std::vector<std::string> asked;
  bool replace = true;

  writeit::SaveDecision decide(const std::string& chosen,
                               const writeit::FileType& type = rtf_file())
  {
    return resolve_save(
        chosen, type,
        [this](const std::string& path) {
          checked.push_back(path);
          if (folders.count(path))
            return writeit::PathKind::Folder;
          return files.count(path) ? writeit::PathKind::File : writeit::PathKind::Missing;
        },
        [this](const std::string& path) {
          asked.push_back(path);
          return replace;
        });
  }

  // The path written, or nothing.
  std::optional<std::string> save(const std::string& chosen)
  {
    const auto decision = decide(chosen);
    if (decision.outcome != writeit::SaveOutcome::Write)
      return std::nullopt;
    return decision.path;
  }
};

void overwrite_order()
{
  {
    // Typed "zout", zout.rtf exists: asked about zout.rtf, not zout.
    Disk disk;
    disk.files = {"/d/zout.rtf"};
    const auto path = disk.save("/d/zout");
    CHECK(path && *path == "/d/zout.rtf");
    CHECK(disk.asked == std::vector<std::string>{"/d/zout.rtf"});
    CHECK(disk.checked == std::vector<std::string>{"/d/zout.rtf"});
  }
  {
    // Declining means choose again, and nothing is written.
    Disk disk;
    disk.files = {"/d/zout.rtf"};
    disk.replace = false;
    CHECK(!disk.save("/d/zout"));
    CHECK(disk.asked == std::vector<std::string>{"/d/zout.rtf"});
  }
  {
    // The doubled name GTK hands back: asked about zout.rtf, which exists,
    // not zout.rtf.rtf, which does not.
    Disk disk;
    disk.files = {"/d/zout.rtf"};
    const auto path = disk.save("/d/zout.rtf.rtf");
    CHECK(path && *path == "/d/zout.rtf");
    CHECK(disk.asked == std::vector<std::string>{"/d/zout.rtf"});
  }
  {
    // A file called plain "zout" is not the one written, so no question.
    Disk disk;
    disk.files = {"/d/zout"};
    const auto path = disk.save("/d/zout");
    CHECK(path && *path == "/d/zout.rtf");
    CHECK(disk.asked.empty());
    CHECK(disk.checked == std::vector<std::string>{"/d/zout.rtf"});
  }
  {
    // zout.txt exists and "zout.txt" was typed; zout.rtf is written, and it
    // is new, so no question.
    Disk disk;
    disk.files = {"/d/zout.txt"};
    const auto path = disk.save("/d/zout.txt");
    CHECK(path && *path == "/d/zout.rtf");
    CHECK(disk.asked.empty());
  }
  {
    // Nothing there: no question.
    Disk disk;
    const auto path = disk.save("/d/my.report");
    CHECK(path && *path == "/d/my.report.rtf");
    CHECK(disk.asked.empty());
  }
}

// The chooser gives no local path for a non-local location. That must not
// become a relative "Untitled.rtf" in whatever folder the app runs in.
void no_local_path()
{
  using writeit::SaveOutcome;
  for (const auto* type : {&rtf_file(), &markdown_file()}) {
    for (const std::string chosen : {"", "Untitled.rtf", "zout", "docs/zout.rtf", "./zout"}) {
      Disk disk;
      disk.files = {"Untitled.rtf", "Untitled.md", "zout.rtf"};
      const auto decision = disk.decide(chosen, *type);
      CHECK(decision.outcome == SaveOutcome::NoPath);
      CHECK(decision.path.empty());
      CHECK(disk.checked.empty());
      CHECK(disk.asked.empty());
    }
  }
  // An absolute path is still fine.
  Disk disk;
  CHECK(disk.decide("/d/zout").outcome == SaveOutcome::Write);
}

// The final name is a folder: no replace question, nothing written, choose
// again.
void folder_named_like_the_file()
{
  using writeit::SaveOutcome;
  {
    Disk disk;
    disk.folders = {"/d/zout.rtf"};
    const auto decision = disk.decide("/d/zout");
    CHECK(decision.outcome == SaveOutcome::Folder);
    CHECK(decision.path == "/d/zout.rtf");
    CHECK(disk.asked.empty());
    CHECK(!disk.save("/d/zout.rtf.rtf"));
    CHECK(disk.asked.empty());
  }
  {
    Disk disk;
    disk.folders = {"/d/notes.md"};
    const auto decision = disk.decide("/d/notes", markdown_file());
    CHECK(decision.outcome == SaveOutcome::Folder);
    CHECK(decision.path == "/d/notes.md");
    CHECK(disk.asked.empty());
  }
  {
    // A folder called plain "zout" is not in the way of zout.rtf.
    Disk disk;
    disk.folders = {"/d/zout"};
    const auto decision = disk.decide("/d/zout");
    CHECK(decision.outcome == SaveOutcome::Write);
    CHECK(decision.path == "/d/zout.rtf");
  }
}

}  // namespace

// Exactly the checks this suite runs, loops included. Update it with the tests.
constexpr int kChecks = 162;

int main()
{
  save_as_names();
  directories();
  export_names();
  idempotent();
  extensions();
  overwrite_order();
  no_local_path();
  folder_named_like_the_file();
  return suite_test::done("filename", kChecks);
}
