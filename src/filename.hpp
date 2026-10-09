/* SPDX-License-Identifier: Unlicense */

#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace writeit {

// What a save writes: the extension it appends, and the extensions a typed
// name may already end in and keep, in any case.
struct FileType {
  std::string ext;
  std::vector<std::string> accepted;
};

// Save and Save As write RTF. Export writes Markdown, as .md or .markdown.
const FileType& rtf_file();
const FileType& markdown_file();

// The lower-case extension of a path's file name, with its dot, or "".
std::string extension_of(const std::string& path);

// The file a Save As or Export writes when the chooser returns `chosen`.
// GTK selects only the stem of the suggested name, so typing "zout.rtf" over
// "Untitled.rtf" comes back as "zout.rtf.rtf". The rule, on the file name
// only (directories are left alone):
// - trailing dots go, and so does a run of document extensions (.rtf, .md,
//   .markdown, .txt) at the end;
// - if the first of those is one the type accepts, the name keeps it as
//   typed ("zout.RTF"); otherwise it gets the type's extension ("zout.txt"
//   saves as "zout.rtf", since Save writes RTF);
// - any other dot is part of the name ("my.report" saves as
//   "my.report.rtf");
// - a name that is only an extension (".rtf") is "Untitled" plus it.
std::string save_name(const std::string& chosen, const FileType& type);

// What is at a path on disk.
enum class PathKind { Missing, File, Folder };

// What a Save As or Export does with the chooser's answer.
enum class SaveOutcome {
  Write,     // write `path`
  Declined,  // `path` exists and the user kept it: choose again
  NoPath,    // no local, absolute path (a non-local location): choose again
  Folder,    // `path` is a folder: say so and choose again
};

struct SaveDecision {
  SaveOutcome outcome;
  std::string path;  // the final name; empty for NoPath
};

// Decides a save. A chosen path that is empty or not absolute is NoPath and
// nothing is asked. `kind_of` and `confirm_replace` are asked about the final
// name, after save_name, never about what was typed, and confirm_replace
// only when that name is an existing file.
SaveDecision resolve_save(const std::string& chosen, const FileType& type,
                          const std::function<PathKind(const std::string&)>& kind_of,
                          const std::function<bool(const std::string&)>& confirm_replace);

}  // namespace writeit
