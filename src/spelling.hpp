/* SPDX-License-Identifier: Unlicense */

#pragma once

#include "document.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace writeit {

// One word a spelling check did not know. `begin` and `end` are byte offsets
// in the paragraph's text, pictures skipped. The word is the text between them.
struct SpellMiss {
  size_t paragraph = 0;
  size_t begin = 0;
  size_t end = 0;
  std::string word;
};

// ASCII letters folded. Other bytes, including the rest of a UTF-8 character,
// stay as they are, so a dictionary can be stored in lowercase.
std::string fold_word(const std::string& word);

// A dictionary aspell will accept as -d: letters, digits, '_' and '-'.
bool dictionary_name_ok(const std::string& name);

// Words `known` rejects. A word is a run of letters. An apostrophe or a
// hyphen stays in the word when it has a letter on each side, so "don't"
// and "well-known" are each one word. A picture is not text. `known` is
// given the word as it is written; fold_word() is how a fixed list compares.
std::vector<SpellMiss> spelling_misses(const std::vector<Paragraph>& paragraphs,
                                       const std::function<bool(const std::string&)>& known);

// Replaces the bytes [begin, end) of the paragraph's text. False when the
// range is not inside that text. A word split across runs is rewritten in
// the first of them and removed from the rest.
bool replace_word(Paragraph& paragraph, size_t begin, size_t end, const std::string& replacement);

// A local aspell process. The spelling check itself never needs one: a test
// passes its own word list to spelling_misses(). This is what the dialog uses.
class AspellDictionary {
 public:
  explicit AspellDictionary(const std::string& name);
  ~AspellDictionary();

  AspellDictionary(const AspellDictionary&) = delete;
  AspellDictionary& operator=(const AspellDictionary&) = delete;

  bool available() const;
  const std::string& error() const;
  bool contains(const std::string& word);
  std::vector<std::string> suggestions(const std::string& word);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace writeit
