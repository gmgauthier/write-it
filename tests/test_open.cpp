/* SPDX-License-Identifier: Unlicense */

// Files handed to the program: `write-it a.rtf b.rtf`, a second launch, the
// file manager's Open With. One document, one window.

#include "check.hpp"
#include "open_plan.hpp"
#include "settings.hpp"

#include <glib.h>
#include <glib/gstdio.h>
#include <glibmm/fileutils.h>
#include <glibmm/miscutils.h>
#include <unistd.h>

#include <string>
#include <vector>

namespace {

using writeit::close_after_open;
using writeit::OpenAction;
using writeit::OpenStep;
using writeit::plan_open;
using writeit::WindowState;

// Action i of a plan matches; false, not a crash, when the plan is short.
bool is(const std::vector<OpenAction>& plan, size_t i, OpenStep step, int window,
        const std::string& path)
{
  if (i >= plan.size())
    return false;
  const OpenAction& action = plan[i];
  return action.step == step && action.window == window && action.path == path;
}

bool step_is(const std::vector<OpenAction>& plan, size_t i, OpenStep step)
{
  return i < plan.size() && plan[i].step == step;
}

const WindowState kPristine{"", true};
const WindowState kEditedUntitled{"", false};

void one_window_per_file()
{
  // First launch with files: no window yet.
  auto plan = plan_open({"/d/a.rtf"}, {});
  CHECK(plan.size() == 1);
  CHECK(is(plan, 0, OpenStep::LoadNew, -1, "/d/a.rtf"));

  // Every file is opened, not just the last.
  plan = plan_open({"/d/a.rtf", "/d/b.rtf", "/d/c.md"}, {});
  CHECK(plan.size() == 3);
  CHECK(is(plan, 0, OpenStep::LoadNew, -1, "/d/a.rtf"));
  CHECK(is(plan, 1, OpenStep::LoadNew, -1, "/d/b.rtf"));
  CHECK(is(plan, 2, OpenStep::LoadNew, -1, "/d/c.md"));

  // Nothing asked for, nothing to do.
  CHECK(plan_open({}, {kPristine}).empty());
}

void pristine_window_is_reused()
{
  // An empty Untitled nobody has typed in takes the first file.
  auto plan = plan_open({"/d/a.rtf"}, {kPristine});
  CHECK(plan.size() == 1);
  CHECK(is(plan, 0, OpenStep::LoadInto, 0, "/d/a.rtf"));

  plan = plan_open({"/d/a.rtf", "/d/b.rtf"}, {kPristine});
  CHECK(plan.size() == 2);
  CHECK(is(plan, 0, OpenStep::LoadInto, 0, "/d/a.rtf"));
  CHECK(is(plan, 1, OpenStep::LoadNew, -1, "/d/b.rtf"));

  // Each pristine window is used once.
  plan = plan_open({"/d/a.rtf", "/d/b.rtf"}, {kPristine, kEditedUntitled, kPristine});
  CHECK(plan.size() == 2);
  CHECK(is(plan, 0, OpenStep::LoadInto, 0, "/d/a.rtf"));
  CHECK(is(plan, 1, OpenStep::LoadInto, 2, "/d/b.rtf"));
}

void documents_are_never_replaced()
{
  // An edited Untitled is a document: the file gets a new window.
  auto plan = plan_open({"/d/a.rtf"}, {kEditedUntitled});
  CHECK(plan.size() == 1);
  CHECK(is(plan, 0, OpenStep::LoadNew, -1, "/d/a.rtf"));

  // So is an open file, saved or not.
  plan = plan_open({"/d/a.rtf"}, {WindowState{"/d/b.rtf", false}});
  CHECK(plan.size() == 1);
  CHECK(is(plan, 0, OpenStep::LoadNew, -1, "/d/a.rtf"));

  // The same file already open, perhaps with unsaved edits: bring that
  // window forward, never reload it.
  plan = plan_open({"/d/a.rtf"}, {kPristine, WindowState{"/d/a.rtf", false}});
  CHECK(plan.size() == 1);
  CHECK(is(plan, 0, OpenStep::Present, 1, "/d/a.rtf"));

  // Asked for twice: opened once.
  plan = plan_open({"/d/a.rtf", "/d/a.rtf"}, {});
  CHECK(plan.size() == 1);
  CHECK(is(plan, 0, OpenStep::LoadNew, -1, "/d/a.rtf"));
}

void not_local()
{
  // No local path (an sftp:// or http:// URI without a mount): refused, and
  // it does not use up the pristine window. It comes after the files that
  // can be opened.
  auto plan = plan_open({"", "/d/a.rtf"}, {kPristine});
  CHECK(plan.size() == 2);
  CHECK(is(plan, 0, OpenStep::LoadInto, 0, "/d/a.rtf"));
  CHECK(step_is(plan, 1, OpenStep::RefuseNotLocal));

  plan = plan_open({""}, {});
  CHECK(plan.size() == 1);
  CHECK(step_is(plan, 0, OpenStep::RefuseNotLocal));
}

void missing_files_last()
{
  // Files that exist open first; missing ones come after, so their error
  // dialogs (modal, as from File > Open) do not hold up the others.
  const auto exists = [](const std::string& path) {
    return path.find("missing") == std::string::npos;
  };
  auto plan = plan_open({"/d/missing.rtf", "/d/a.rtf", "", "/d/b.rtf"}, {kPristine}, exists);
  CHECK(plan.size() == 4);
  CHECK(is(plan, 0, OpenStep::LoadInto, 0, "/d/a.rtf"));
  CHECK(is(plan, 1, OpenStep::LoadNew, -1, "/d/b.rtf"));
  CHECK(step_is(plan, 2, OpenStep::RefuseNotLocal));
  CHECK(is(plan, 3, OpenStep::LoadNew, -1, "/d/missing.rtf"));
  // Only a missing file: it may take the pristine window, which stays.
  plan = plan_open({"/d/missing.rtf"}, {kPristine}, exists);
  CHECK(plan.size() == 1);
  CHECK(is(plan, 0, OpenStep::LoadInto, 0, "/d/missing.rtf"));
}

void imports_are_found_again()
{
  // An imported .txt or .md keeps no save path (Save writes a new .rtf), but
  // the window remembers where it came from: asking for it again brings
  // that window forward instead of importing a second copy.
  const WindowState notes{"", false, "/d/n.txt"};
  auto plan = plan_open({"/d/n.txt"}, {notes});
  CHECK(plan.size() == 1);
  CHECK(is(plan, 0, OpenStep::Present, 0, "/d/n.txt"));
  plan = plan_open({"/d/m.md", "/d/n.txt"}, {kPristine, notes});
  CHECK(plan.size() == 2);
  CHECK(is(plan, 0, OpenStep::LoadInto, 0, "/d/m.md"));
  CHECK(is(plan, 1, OpenStep::Present, 1, "/d/n.txt"));
  // Another file of the same name elsewhere is another file.
  plan = plan_open({"/e/n.txt"}, {notes});
  CHECK(is(plan, 0, OpenStep::LoadNew, -1, "/e/n.txt"));
}

// "Already open" is the file itself, not the name it was asked for by: a
// symlink, a hard link, or a path through "." or ".." to an open file
// brings its window forward. Real files, in a scratch folder.
void same_file_by_identity()
{
  std::string dir = Glib::build_filename(Glib::get_tmp_dir(), "write-it-open-XXXXXX");
  if (!g_mkdtemp(&dir[0])) {
    CHECK(false);
    return;
  }
  const std::string a = Glib::build_filename(dir, "a.rtf");
  const std::string copy = Glib::build_filename(dir, "copy.rtf");
  const std::string sym = Glib::build_filename(dir, "link.rtf");
  const std::string hard = Glib::build_filename(dir, "hard.rtf");
  const std::string sub = Glib::build_filename(dir, "sub");
  const std::string notes = Glib::build_filename(dir, "notes.txt");
  const std::string notes_link = Glib::build_filename(dir, "notes-link.txt");
  Glib::file_set_contents(a, "{\\rtf1 A.}");
  Glib::file_set_contents(copy, "{\\rtf1 A.}");
  Glib::file_set_contents(notes, "Notes.\n");
  g_mkdir(sub.c_str(), 0700);
  const bool made = symlink("a.rtf", sym.c_str()) == 0 && link(a.c_str(), hard.c_str()) == 0 &&
                    symlink("notes.txt", notes_link.c_str()) == 0;
  CHECK(made);
  const std::string dotted = dir + "/./a.rtf";
  const std::string up = sub + "/../a.rtf";

  const WindowState open_a{a, false};
  for (const std::string& name : {sym, hard, dotted, up}) {
    auto plan = plan_open({name}, {open_a});
    CHECK(plan.size() == 1);
    CHECK(is(plan, 0, OpenStep::Present, 0, name));
  }
  // Asked for under four names at once, it is opened once.
  auto plan = plan_open({a, sym, hard, up}, {kPristine});
  CHECK(plan.size() == 1);
  CHECK(is(plan, 0, OpenStep::LoadInto, 0, a));
  // An import is found by its file too.
  plan = plan_open({notes_link}, {WindowState{"", false, notes}});
  CHECK(plan.size() == 1);
  CHECK(is(plan, 0, OpenStep::Present, 0, notes_link));
  // A copy, the same bytes in another file, is another file.
  plan = plan_open({copy}, {open_a});
  CHECK(is(plan, 0, OpenStep::LoadNew, -1, copy));

  for (const std::string& file : {sym, hard, notes_link, a, copy, notes})
    g_remove(file.c_str());
  g_rmdir(sub.c_str());
  g_rmdir(dir.c_str());
}

void refusals_name_the_uri()
{
  auto plan = plan_open({writeit::OpenRequest{"", "sftp://example.org/x.rtf"}}, {});
  CHECK(plan.size() == 1);
  CHECK(step_is(plan, 0, OpenStep::RefuseNotLocal));
  CHECK(plan.size() == 1 && plan[0].uri == "sftp://example.org/x.rtf");
}

void messages()
{
  // Word-style, naming the file; the name, not the whole path, for a local
  // file, and the URI for a remote one.
  CHECK(writeit::missing_message("/home/greg/docs/missing.rtf") ==
        "Could not find the file \u201cmissing.rtf\u201d.");
  CHECK(writeit::unreadable_message("/home/greg/docs/broken.rtf") ==
        "Could not open the file \u201cbroken.rtf\u201d.");
  CHECK(writeit::missing_message("/tmp/a b \u00e9.rtf") ==
        "Could not find the file \u201ca b \u00e9.rtf\u201d.");
  CHECK(writeit::not_local_message("sftp://example.org/x.rtf") ==
        "Write-It can only open files on this computer: sftp://example.org/x.rtf");
}

void failed_windows_close()
{
  // A window made for a file that failed is closed when another window is
  // left, so no empty window is left behind.
  CHECK(close_after_open(1, {true}) == std::vector<bool>{true});
  CHECK(close_after_open(1, {false, true}) == (std::vector<bool>{false, true}));
  CHECK(close_after_open(0, {false, true}) == (std::vector<bool>{false, true}));
  CHECK(close_after_open(0, {true, false}) == (std::vector<bool>{true, false}));
  // Unless it is the only window: the program keeps one to show the error
  // and to be used.
  CHECK(close_after_open(0, {true}) == std::vector<bool>{false});
  CHECK(close_after_open(0, {true, true, true}) == (std::vector<bool>{false, true, true}));
  // Windows that loaded stay.
  CHECK(close_after_open(0, {false, false}) == (std::vector<bool>{false, false}));
  CHECK(close_after_open(2, {}).empty());
}

void recent_files()
{
  using writeit::push_recent;
  using V = std::vector<std::string>;
  CHECK(push_recent({}, "/d/a.rtf", 8) == V{"/d/a.rtf"});
  CHECK(push_recent({"/d/a.rtf"}, "/d/b.rtf", 8) == (V{"/d/b.rtf", "/d/a.rtf"}));
  // Opened again: moves to the top, once.
  CHECK(push_recent({"/d/b.rtf", "/d/a.rtf"}, "/d/a.rtf", 8) == (V{"/d/a.rtf", "/d/b.rtf"}));
  // Cut to the count.
  CHECK(push_recent({"/d/1", "/d/2", "/d/3", "/d/4"}, "/d/5", 4) ==
        (V{"/d/5", "/d/1", "/d/2", "/d/3"}));
}

}  // namespace

// Exactly the checks this suite runs. Update it with the tests.
constexpr int kChecks = 74;

int main()
{
  one_window_per_file();
  pristine_window_is_reused();
  documents_are_never_replaced();
  not_local();
  missing_files_last();
  imports_are_found_again();
  same_file_by_identity();
  refusals_name_the_uri();
  messages();
  failed_windows_close();
  recent_files();
  return suite_test::done("open", kChecks);
}
