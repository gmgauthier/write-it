/* SPDX-License-Identifier: Unlicense */

#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace writeit {

// Opening files handed to the program: `write-it a.rtf b.md`, a second
// launch while it runs, or the file manager's Open and Open With. One
// document, one window: each file gets a window, and no window holding a
// document is ever reloaded, so nothing unsaved is lost.

// A window as the planner sees it.
struct WindowState {
  std::string path;       // the RTF file it saves to, or "" (Untitled or an import)
  bool pristine = false;  // Untitled, never edited: a new launch's empty window
  std::string source{};   // the .md or .txt it was imported from, or ""
};

// A file handed to the program: its local path ("" when it has none) and
// its URI, which names it in a refusal.
struct OpenRequest {
  std::string path;
  std::string uri;
  OpenRequest(std::string p, std::string u)
      : path(std::move(p)),
        uri(std::move(u))
  {
  }
  // A local file, named by its path.
  OpenRequest(const std::string& p)
      : path(p),
        uri(p)
  {
  }  // NOLINT: implicit on purpose
  OpenRequest(const char* p)
      : path(p),
        uri(p)
  {
  }  // NOLINT: implicit on purpose
};

enum class OpenStep {
  Present,         // the file is already open in `window`: bring it forward
  LoadInto,        // load into `window`, a pristine one
  LoadNew,         // load into a new window
  RefuseNotLocal,  // no local path (a non-local URI): say so, open nothing
};

struct OpenAction {
  OpenStep step;
  int window = -1;  // index into the windows given, for Present and LoadInto
  std::string path;
  std::string uri{};  // for RefuseNotLocal
};

// One action per requested file. A request's path comes from
// Gio::File::get_path(), "" for a file with none. A file already open
// (saved to, or imported from, that path), or
// asked for twice, is presented rather than loaded again. Each pristine
// window may take one file; the others get new windows. Files that exist
// come first, in order, then refusals, then missing files, so the error
// dialogs come after everything that could be opened.
std::vector<OpenAction> plan_open(
    const std::vector<OpenRequest>& requests, const std::vector<WindowState>& windows,
    const std::function<bool(const std::string&)>& exists = [](const std::string&) {
      return true;
    });

// After the actions run: for each window created for them, in order,
// whether it failed to load (missing, unreadable, not a document) and so
// holds only an empty Untitled. Returns which of those to close: all of
// them while any other window remains, else all but the first, so the
// program always has a window to show its error in.
std::vector<bool> close_after_open(int existing_windows, const std::vector<bool>& created_failed);

// The open errors, Word-style, naming the file: by its name for a local
// file, by its URI for one with no local path. File > Open, Open Recent and
// the command line all say these.
std::string missing_message(const std::string& path);
std::string unreadable_message(const std::string& path);
std::string not_local_message(const std::string& uri);

}  // namespace writeit
