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

// The path to write, or nothing when the user declines to replace a file
// and should choose again. `exists` and `confirm_replace` are asked about
// the final name, after save_name, never about what was typed.
std::optional<std::string> resolve_save(
    const std::string& chosen, const FileType& type,
    const std::function<bool(const std::string&)>& exists,
    const std::function<bool(const std::string&)>& confirm_replace);

}  // namespace writeit
