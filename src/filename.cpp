/* SPDX-License-Identifier: Unlicense */

#include "filename.hpp"

#include <algorithm>
#include <cctype>

namespace writeit {

namespace {

std::string lower(std::string text)
{
  for (char& c : text)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return text;
}

// The document extensions a typed name may end in.
const std::vector<std::string>& document_extensions()
{
  static const std::vector<std::string> list = {".rtf", ".md", ".markdown", ".txt"};
  return list;
}

// How many trailing characters of `name` are a document extension, dot
// included, or 0.
size_t document_extension(const std::string& name)
{
  const auto dot = name.rfind('.');
  if (dot == std::string::npos)
    return 0;
  const std::string ext = lower(name.substr(dot));
  const auto& list = document_extensions();
  return std::find(list.begin(), list.end(), ext) != list.end() ? name.size() - dot : 0;
}

void strip_dots(std::string& name)
{
  while (!name.empty() && name.back() == '.')
    name.pop_back();
}

}  // namespace

const FileType& rtf_file()
{
  static const FileType type{".rtf", {".rtf"}};
  return type;
}

const FileType& markdown_file()
{
  static const FileType type{".md", {".md", ".markdown"}};
  return type;
}

std::string extension_of(const std::string& path)
{
  const auto slash = path.rfind('/');
  const std::string base = slash == std::string::npos ? path : path.substr(slash + 1);
  const auto dot = base.rfind('.');
  if (dot == std::string::npos)
    return {};
  return lower(base.substr(dot));
}

std::string save_name(const std::string& chosen, const FileType& type)
{
  const auto slash = chosen.rfind('/');
  const std::string dir = slash == std::string::npos ? std::string() : chosen.substr(0, slash + 1);
  std::string base = slash == std::string::npos ? chosen : chosen.substr(slash + 1);

  // Peel trailing dots and document extensions. The last one peeled is the
  // first one typed; anything after it is GTK's leftover or a repeat.
  strip_dots(base);
  std::string typed;
  while (const size_t n = document_extension(base)) {
    typed = base.substr(base.size() - n);
    base.erase(base.size() - n);
    strip_dots(base);
  }
  if (base.empty())
    base = "Untitled";
  const auto& ok = type.accepted;
  if (!typed.empty() && std::find(ok.begin(), ok.end(), lower(typed)) != ok.end())
    return dir + base + typed;
  return dir + base + type.ext;
}

std::optional<std::string> resolve_save(
    const std::string& chosen, const FileType& type,
    const std::function<bool(const std::string&)>& exists,
    const std::function<bool(const std::string&)>& confirm_replace)
{
  const std::string path = save_name(chosen, type);
  if (exists(path) && !confirm_replace(path))
    return std::nullopt;
  return path;
}

}  // namespace writeit
