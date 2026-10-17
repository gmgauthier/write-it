/* SPDX-License-Identifier: Unlicense */

#include "check.hpp"
#include "document.hpp"

#include <cctype>
#include <string>

constexpr int kChecks = 113;

namespace {

const char* kHead = "{\\rtf1\\ansi\\ansicpg1252\\deff0{\\fonttbl{\\f0\\fswiss Sans;}}";

bool has_word(const std::string& text, const std::string& control)
{
  size_t at = 0;
  while ((at = text.find(control, at)) != std::string::npos) {
    const size_t next = at + control.size();
    if (next >= text.size() || !std::isalpha(static_cast<unsigned char>(text[next])))
      return true;
    at = next;
  }
  return false;
}

writeit::Run text_run(const char* text)
{
  writeit::Run run;
  run.font = "Sans";
  run.size = 11;
  run.text = text;
  return run;
}

writeit::Paragraph body(const char* text)
{
  writeit::Paragraph paragraph;
  paragraph.runs.push_back(text_run(text));
  return paragraph;
}

writeit::TabStop stop(int position, writeit::TabAlign align, writeit::TabLeader leader)
{
  writeit::TabStop tab;
  tab.position = position;
  tab.align = align;
  tab.leader = leader;
  return tab;
}

writeit::Style* normal_of(writeit::Document& doc)
{
  for (writeit::Style& style : doc.styles) {
    if (style.name == writeit::kNormalStyle)
      return &style;
  }
  return nullptr;
}

}  // namespace

int main()
{
  using namespace writeit;

  {
    Document doc = blank_document("Sans", 11);
    Paragraph paragraph = body("Stops");
    paragraph.tabs = {stop(720, TabAlign::Left, TabLeader::None),
                      stop(1440, TabAlign::Center, TabLeader::Dot),
                      stop(2160, TabAlign::Right, TabLeader::Hyphen),
                      stop(2880, TabAlign::Decimal, TabLeader::Underline),
                      stop(3600, TabAlign::Bar, TabLeader::Dot)};
    doc.paragraphs = {paragraph};
    const std::string rtf = rtf_export(doc);
    CHECK(has_word(rtf, "\\tx720"));
    CHECK(has_word(rtf, "\\tqc"));
    CHECK(has_word(rtf, "\\tldot"));
    CHECK(has_word(rtf, "\\tx1440"));
    CHECK(has_word(rtf, "\\tqr"));
    CHECK(has_word(rtf, "\\tlhyph"));
    CHECK(has_word(rtf, "\\tx2160"));
    CHECK(has_word(rtf, "\\tqdec"));
    CHECK(has_word(rtf, "\\tlul"));
    CHECK(has_word(rtf, "\\tx2880"));
    CHECK(has_word(rtf, "\\tb"));
    CHECK(has_word(rtf, "\\tx3600"));
    Document back;
    CHECK(rtf_import(rtf, back));
    CHECK(back.paragraphs.size() == 1);
    std::vector<TabStop> want = paragraph.tabs;
    want[4].leader = TabLeader::None;
    CHECK(back.paragraphs[0].tabs == clamp_tabs(want));
  }

  {
    Document doc = blank_document("Sans", 11);
    doc.paragraphs = {body("Plain")};
    const std::string rtf = rtf_export(doc);
    CHECK(!has_word(rtf, "\\tx"));
    CHECK(!has_word(rtf, "\\deftab"));
  }

  {
    const std::string rtf = rtf_export(blank_document("Sans", 11));
    CHECK(!has_word(rtf, "\\tx"));
    CHECK(!has_word(rtf, "\\deftab"));
  }

  {
    Document doc = blank_document("Sans", 11);
    CHECK(!has_word(rtf_export(doc), "\\deftab"));
    doc.default_tab = 360;
    const std::string rtf = rtf_export(doc);
    CHECK(has_word(rtf, "\\deftab360"));
    Document back;
    CHECK(rtf_import(rtf, back));
    CHECK(back.default_tab == 360);
    doc.default_tab = kDefaultTab;
    CHECK(!has_word(rtf_export(doc), "\\deftab"));
  }

  {
    Document back;
    const std::string rtf = std::string(kHead) + "\\tqr\\tldot\\pard\\tx720 Hello\\par}";
    CHECK(rtf_import(rtf, back));
    CHECK(back.paragraphs.size() == 1);
    CHECK(back.paragraphs[0].tabs.size() == 1);
    CHECK(back.paragraphs[0].tabs[0].position == 720);
    CHECK(back.paragraphs[0].tabs[0].align == TabAlign::Left);
    CHECK(back.paragraphs[0].tabs[0].leader == TabLeader::None);
  }

  {
    const std::string rtf =
        std::string(kHead) + "{\\stylesheet{\\s0\\tx1440 Normal;}}\\pard Hello\\par}";
    Document back;
    CHECK(rtf_import(rtf, back));
    CHECK(back.paragraphs.size() == 1);
    CHECK(back.paragraphs[0].tabs.size() == 1);
    CHECK(back.paragraphs[0].tabs[0].position == 1440);
    const Style* normal = find_style(back.styles, kNormalStyle);
    CHECK(normal != nullptr);
    CHECK(normal && normal->tabs.size() == 1);
    CHECK(normal && normal->tabs[0].position == 1440);
  }

  {
    const std::string rtf =
        std::string(kHead) + "{\\stylesheet{\\s0\\tx1440 Normal;}}\\pard\\tx720 Hello\\par}";
    Document back;
    CHECK(rtf_import(rtf, back));
    CHECK(back.paragraphs.size() == 1);
    CHECK(back.paragraphs[0].tabs.size() == 1);
    CHECK(back.paragraphs[0].tabs[0].position == 720);
    CHECK(back.paragraphs[0].tabs[0].align == TabAlign::Left);
  }

  {
    Document doc = blank_document("Sans", 11);
    doc.styles = default_styles();
    Style* normal = normal_of(doc);
    CHECK(normal != nullptr);
    if (normal)
      normal->tabs = {stop(1440, TabAlign::Right, TabLeader::Dot)};
    doc.paragraphs = {body("Styled")};
    const std::string rtf = rtf_export(doc);
    CHECK(has_word(rtf, "\\stylesheet"));
    CHECK(has_word(rtf, "\\tqr"));
    CHECK(has_word(rtf, "\\tldot"));
    CHECK(has_word(rtf, "\\tx1440"));
    Document back;
    CHECK(rtf_import(rtf, back));
    const Style* got = find_style(back.styles, kNormalStyle);
    CHECK(got != nullptr);
    CHECK(got && got->tabs == std::vector<TabStop>{stop(1440, TabAlign::Right, TabLeader::Dot)});
    // No \\tx on the paragraph, so the style's stops are the paragraph's on the way back in.
    CHECK(back.paragraphs.size() == 1);
    CHECK(back.paragraphs[0].tabs == std::vector<TabStop>{stop(1440, TabAlign::Right, TabLeader::Dot)});
  }

  {
    Document doc = blank_document("Sans", 11);
    doc.styles = default_styles();
    Style* normal = normal_of(doc);
    CHECK(normal != nullptr);
    if (normal)
      normal->tabs = {stop(1440, TabAlign::Left, TabLeader::None)};
    Paragraph own = body("Own");
    own.tabs = {stop(720, TabAlign::Center, TabLeader::Hyphen)};
    doc.paragraphs = {own};
    const std::string rtf = rtf_export(doc);
    CHECK(has_word(rtf, "\\tqc"));
    CHECK(has_word(rtf, "\\tlhyph"));
    CHECK(has_word(rtf, "\\tx720"));
    Document back;
    CHECK(rtf_import(rtf, back));
    CHECK(back.paragraphs[0].tabs == own.tabs);
    const Style* got = find_style(back.styles, kNormalStyle);
    CHECK(got && got->tabs == std::vector<TabStop>{stop(1440, TabAlign::Left, TabLeader::None)});
  }

  {
    // An empty list cannot cancel a style that has stops. The file has no word for it.
    Document doc = blank_document("Sans", 11);
    doc.styles = default_styles();
    if (Style* normal = normal_of(doc))
      normal->tabs = {stop(1440, TabAlign::Left, TabLeader::None)};
    doc.paragraphs = {body("Cleared")};
    const std::string rtf = rtf_export(doc);
    CHECK(has_word(rtf, "\\tx1440"));
    Document back;
    CHECK(rtf_import(rtf, back));
    CHECK(back.paragraphs[0].tabs.size() == 1);
    CHECK(back.paragraphs[0].tabs[0].position == 1440);
  }

  {
    Document back;
    CHECK(rtf_import(std::string(kHead) + "\\tb\\tldot\\tx900 Bar\\par}", back));
    CHECK(back.paragraphs[0].tabs.size() == 1);
    CHECK(back.paragraphs[0].tabs[0].align == TabAlign::Bar);
    CHECK(back.paragraphs[0].tabs[0].leader == TabLeader::None);
    CHECK(back.paragraphs[0].tabs[0].position == 900);
  }

  {
    Document back;
    CHECK(rtf_import(std::string(kHead) + "\\tlth\\tx1800 Thick\\par}", back));
    CHECK(back.paragraphs[0].tabs.size() == 1);
    CHECK(back.paragraphs[0].tabs[0].leader == TabLeader::Underline);
    CHECK(back.paragraphs[0].tabs[0].align == TabAlign::Left);
    CHECK(back.paragraphs[0].tabs[0].position == 1800);
  }

  {
    Document back;
    CHECK(rtf_import(std::string(kHead) + "\\tx720{\\tx1440}Hello\\par}", back));
    CHECK(back.paragraphs[0].tabs.size() == 1);
    CHECK(back.paragraphs[0].tabs[0].position == 720);
  }

  {
    Document back;
    CHECK(rtf_import(std::string(kHead) + "A\\tab B\\par}", back));
    CHECK(back.paragraphs.size() == 1);
    CHECK(back.paragraphs[0].cell.table == 0);
    bool saw = false;
    for (const Run& run : back.paragraphs[0].runs) {
      if (run.text.find('\t') != std::string::npos)
        saw = true;
    }
    CHECK(saw);
  }

  {
    std::string rtf = kHead;
    for (int i = 1; i <= 51; ++i)
      rtf += "\\tx" + std::to_string(i * 100);
    rtf += " Many\\par}";
    Document back;
    CHECK(rtf_import(rtf, back));
    CHECK(back.paragraphs[0].tabs.size() == 50);
    CHECK(back.paragraphs[0].tabs.front().position == 100);
    CHECK(back.paragraphs[0].tabs.back().position == 5000);
  }

  {
    CHECK(tab_at({}, 720, 0) == 720);
    CHECK(tab_at({}, 720, 720) == 720);
    CHECK(tab_at({}, 720, 800) == 1440);
    CHECK(tab_at({}, 720, 1440) == 1440);
    CHECK(tab_at({}, 720, -40) == 720);
    const std::vector<TabStop> at1000 = {stop(1000, TabAlign::Left, TabLeader::None)};
    CHECK(tab_at(at1000, 720, 0) == 1000);
    CHECK(tab_at(at1000, 720, 1000) == 1000);
    CHECK(tab_at(at1000, 720, 1001) == 1440);
    const std::vector<TabStop> at720 = {stop(720, TabAlign::Left, TabLeader::None)};
    CHECK(tab_at(at720, 720, 721) == 1440);
    const std::vector<TabStop> bar = {stop(1000, TabAlign::Bar, TabLeader::Dot)};
    CHECK(tab_at(bar, 720, 0) == 1440);
    const std::vector<TabStop> mixed = {stop(500, TabAlign::Bar, TabLeader::None),
                                        stop(1000, TabAlign::Left, TabLeader::None)};
    CHECK(tab_at(mixed, 720, 0) == 1000);
    CHECK(tab_at(mixed, 720, 600) == 1000);

    const std::vector<TabStop> kept =
        clamp_tabs({stop(100, TabAlign::Left, TabLeader::None),
                    stop(100, TabAlign::Right, TabLeader::Dot)});
    CHECK(kept.size() == 1);
    CHECK(kept[0].align == TabAlign::Right);
    CHECK(kept[0].leader == TabLeader::Dot);
    CHECK(clamp_tabs({stop(200, TabAlign::Bar, TabLeader::Dot)})[0].leader == TabLeader::None);
    CHECK(clamp_tabs({stop(-5, TabAlign::Left, TabLeader::None)})[0].position == 0);
    CHECK(clamp_tabs({stop(kMaxIndent + 50, TabAlign::Left, TabLeader::None)})[0].position ==
          kMaxIndent);
    const std::vector<TabStop> fixed = clamp_tabs(
        {stop(10, static_cast<TabAlign>(40), static_cast<TabLeader>(40))});
    CHECK(fixed[0].align == TabAlign::Left);
    CHECK(fixed[0].leader == TabLeader::None);
    std::vector<TabStop> many;
    for (int i = 0; i < 51; ++i)
      many.push_back(stop(i * 10, TabAlign::Left, TabLeader::None));
    const std::vector<TabStop> capped = clamp_tabs(many);
    CHECK(capped.size() == 50);
    CHECK(capped.back().position == 490);
    CHECK(clamp_default_tab(0) == kDefaultTab);
    CHECK(clamp_default_tab(-3) == kDefaultTab);
    CHECK(clamp_default_tab(kMaxIndent + 1) == kDefaultTab);
    CHECK(clamp_default_tab(1) == 1);
    CHECK(clamp_default_tab(360) == 360);
    CHECK(clamp_default_tab(kDefaultTab) == kDefaultTab);
    CHECK(clamp_default_tab(kMaxIndent) == kMaxIndent);
  }

  return suite_test::done("tabs", kChecks);
}
