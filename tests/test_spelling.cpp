/* SPDX-License-Identifier: Unlicense */

// The spelling check against a fixed word list. No dictionary and no network:
// the dialog's aspell process is not part of this suite.

#include "check.hpp"
#include "spelling.hpp"

#include <functional>
#include <string>
#include <vector>

namespace {

using writeit::Paragraph;
using writeit::Run;
using writeit::SpellMiss;

Run text(const char* value)
{
  Run run;
  run.text = value;
  return run;
}

Paragraph words(std::initializer_list<const char*> parts)
{
  Paragraph paragraph;
  for (const char* part : parts)
    paragraph.runs.push_back(text(part));
  return paragraph;
}

std::function<bool(const std::string&)> list(std::initializer_list<const char*> known)
{
  std::vector<std::string> words;
  for (const char* word : known)
    words.emplace_back(word);
  return [words](const std::string& word) {
    const std::string folded = writeit::fold_word(word);
    for (const std::string& item : words) {
      if (folded == item)
        return true;
    }
    return false;
  };
}

}  // namespace

constexpr int kChecks = 40;

int main()
{
  CHECK(writeit::fold_word("Cat") == "cat");
  CHECK(writeit::fold_word("caf\u00e9") == "caf\u00e9");
  CHECK(writeit::dictionary_name_ok("en"));
  CHECK(writeit::dictionary_name_ok("en_GB"));
  CHECK(writeit::dictionary_name_ok("en-US"));
  CHECK(!writeit::dictionary_name_ok(""));
  CHECK(!writeit::dictionary_name_ok("en US"));
  CHECK(!writeit::dictionary_name_ok("en/US"));
  CHECK(!writeit::dictionary_name_ok(std::string(41, 'a')));

  const auto known = list({"the", "cat", "don't"});
  {
    const std::vector<Paragraph> doc{words({"The cat sat."})};
    const std::vector<SpellMiss> misses = writeit::spelling_misses(doc, known);
    CHECK(misses.size() == 1);
    CHECK(misses[0].paragraph == 0);
    CHECK(misses[0].word == "sat");
    CHECK(misses[0].begin == 8);
    CHECK(misses[0].end == 11);
  }
  CHECK(writeit::spelling_misses({words({"Cat"})}, known).empty());
  {
    const std::vector<SpellMiss> misses = writeit::spelling_misses({words({"don't"})}, known);
    CHECK(misses.empty());
  }
  {
    const std::vector<SpellMiss> misses =
        writeit::spelling_misses({words({"don't"})}, list({"the"}));
    CHECK(misses.size() == 1);
    CHECK(misses[0].word == "don't");
  }
  {
    const std::vector<SpellMiss> misses =
        writeit::spelling_misses({words({"red blue"})}, list({}));
    CHECK(misses.size() == 2);
    CHECK(misses[0].word == "red");
    CHECK(misses[1].word == "blue");
    CHECK(misses[0].begin == 0);
    CHECK(misses[1].begin == 4);
  }
  CHECK(writeit::spelling_misses({words({"1994"})}, known).empty());
  CHECK(writeit::spelling_misses({Paragraph{}}, known).empty());
  {
    Paragraph cell = words({"asdf"});
    cell.cell.table = 1;
    const std::vector<SpellMiss> misses = writeit::spelling_misses({cell}, known);
    CHECK(misses.size() == 1);
    CHECK(misses[0].word == "asdf");
  }
  {
    Paragraph paragraph = words({"The "});
    Run picture;
    picture.image = writeit::Image{};
    paragraph.runs.push_back(picture);
    paragraph.runs.push_back(text("sat"));
    const std::vector<SpellMiss> misses = writeit::spelling_misses({paragraph}, known);
    CHECK(misses.size() == 1);
    CHECK(misses[0].word == "sat");
    CHECK(misses[0].begin == 4);
  }
  {
    Paragraph second = words({"Hello"});
    const std::vector<SpellMiss> misses =
        writeit::spelling_misses({words({"The"}), second}, known);
    CHECK(misses.size() == 1);
    CHECK(misses[0].paragraph == 1);
    CHECK(misses[0].word == "Hello");
  }

  {
    Paragraph paragraph = words({"The cat sat."});
    CHECK(writeit::replace_word(paragraph, 8, 11, "mat"));
    CHECK(paragraph.runs[0].text == "The cat mat.");
  }
  {
    Paragraph paragraph = words({"ca", "t sat"});
    CHECK(writeit::replace_word(paragraph, 0, 3, "dog"));
    CHECK(paragraph.runs[0].text == "dog");
    CHECK(paragraph.runs[1].text == " sat");
  }
  {
    Paragraph paragraph = words({"sat"});
    CHECK(!writeit::replace_word(paragraph, 0, 4, "x"));
    CHECK(paragraph.runs[0].text == "sat");
  }

  return suite_test::done("spelling", kChecks);
}
