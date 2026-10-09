/* SPDX-License-Identifier: Unlicense */

#include "filename.hpp"

namespace writeit {

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

std::string extension_of(const std::string&)
{
  return {};
}

std::string save_name(const std::string& chosen, const FileType&)
{
  return chosen;
}

std::optional<std::string> resolve_save(const std::string& chosen, const FileType&,
                                        const std::function<bool(const std::string&)>&,
                                        const std::function<bool(const std::string&)>&)
{
  return chosen;
}

}  // namespace writeit
