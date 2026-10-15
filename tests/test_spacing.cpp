/* SPDX-License-Identifier: Unlicense */

#include "check.hpp"
#include "document.hpp"
#include "para_check.hpp"

#include <cctype>
#include <string>

constexpr int kChecks = 68;

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

writeit::Paragraph line(const char* text, writeit::Spacing spacing)
{
  writeit::Paragraph paragraph;
  paragraph.spacing = spacing;
  paragraph.runs.push_back(text_run(text));
  return paragraph;
}

writeit::Spacing space(int before, int after, writeit::LineRule rule, int value)
{
  writeit::Spacing spacing;
  spacing.before = before;
  spacing.after = after;
  spacing.rule = rule;
  spacing.value = value;
  return spacing;
}

}  // namespace

int main()
{
  using namespace writeit;

  {
    Document doc = blank_document("Sans", 11);
    doc.paragraphs.clear();
    doc.paragraphs.push_back(line("Before", space(360, 0, LineRule::Single, 240)));
    doc.paragraphs.push_back(line("After", space(0, 240, LineRule::Single, 240)));
    doc.paragraphs.push_back(line("Both", space(120, 180, LineRule::OneHalf, 240)));
    doc.paragraphs.push_back(line("Double", space(0, 0, LineRule::Double, 999)));
    doc.paragraphs.push_back(line("Least", space(0, 0, LineRule::AtLeast, 400)));
    doc.paragraphs.push_back(line("Exact", space(0, 0, LineRule::Exactly, 360)));
    doc.paragraphs.push_back(line("Multi", space(200, 200, LineRule::Multiple, 720)));
    Document back;
    CHECK(rtf_import(rtf_export(doc), back));
    CHECK(back.paragraphs.size() == doc.paragraphs.size());
    bool same = back.paragraphs.size() == doc.paragraphs.size();
    for (size_t i = 0; same && i < doc.paragraphs.size(); ++i)
      same = back.paragraphs[i].spacing == doc.paragraphs[i].spacing;
    CHECK(same);
    CHECK(back.paragraphs.size() == 7 && back.paragraphs[3].spacing.rule == LineRule::Double &&
          back.paragraphs[3].spacing.value == 240);
    CHECK(back.paragraphs.size() == 7 && back.paragraphs[6].spacing.rule == LineRule::Multiple &&
          back.paragraphs[6].spacing.value == 720);
  }

  {
    Document doc;
    const std::string head = kHead;
    CHECK(rtf_import(head + "\\pard\\sl0\\slmult1 Zero\\par}", doc));
    CHECK(doc.paragraphs.size() == 1 && doc.paragraphs[0].spacing.rule == LineRule::Single);

    CHECK(rtf_import(head + "\\pard\\sl240\\slmult1 One\\par}", doc));
    CHECK(doc.paragraphs[0].spacing.rule == LineRule::Single);

    CHECK(rtf_import(head + "\\pard\\sl360\\slmult1 Half\\par}", doc));
    CHECK(doc.paragraphs[0].spacing.rule == LineRule::OneHalf);

    CHECK(rtf_import(head + "\\pard\\sl480\\slmult1 Dub\\par}", doc));
    CHECK(doc.paragraphs[0].spacing.rule == LineRule::Double);

    CHECK(rtf_import(head + "\\pard\\sl240 Least\\par}", doc));
    CHECK(doc.paragraphs[0].spacing.rule == LineRule::AtLeast && doc.paragraphs[0].spacing.value == 240);

    CHECK(rtf_import(head + "\\pard\\sl-480 Tight\\par}", doc));
    CHECK(doc.paragraphs[0].spacing.rule == LineRule::Exactly && doc.paragraphs[0].spacing.value == 480);

    CHECK(rtf_import(head + "\\pard\\sl720\\slmult1 Triple\\par}", doc));
    CHECK(doc.paragraphs[0].spacing.rule == LineRule::Multiple && doc.paragraphs[0].spacing.value == 720);

    CHECK(rtf_import(head + "\\pard\\sb360\\sa180 Gap\\par}", doc));
    CHECK(doc.paragraphs[0].spacing.before == 360 && doc.paragraphs[0].spacing.after == 180);

    CHECK(rtf_import(head + "\\pard\\sb200\\sa100\\sl480\\slmult1 one\\par two\\par \\pard three\\par}",
                     doc));
    CHECK(doc.paragraphs.size() == 3);
    CHECK(doc.paragraphs[0].spacing.before == 200 && doc.paragraphs[0].spacing.after == 100 &&
          doc.paragraphs[0].spacing.rule == LineRule::Double);
    CHECK(doc.paragraphs[1].spacing.before == 200 && doc.paragraphs[1].spacing.rule == LineRule::Double);
    CHECK(doc.paragraphs[2].spacing == Spacing{});

    CHECK(rtf_import(head + "\\pard\\sl-200\\slmult1 Still\\par}", doc));
    CHECK(doc.paragraphs[0].spacing.rule == LineRule::Exactly && doc.paragraphs[0].spacing.value == 200);
  }

  {
    Document doc = blank_document("Sans", 11);
    doc.paragraphs[0].runs.push_back(text_run("Hello"));
    const std::string written = rtf_export(doc);
    CHECK(!has_word(written, "\\sb"));
    CHECK(!has_word(written, "\\sa"));
    CHECK(!has_word(written, "\\sl"));
    CHECK(!has_word(written, "\\slmult"));
    Document back;
    CHECK(rtf_import(written, back));
    CHECK(back.paragraphs.size() == 1 && back.paragraphs[0].spacing == Spacing{});
  }

  {
    Spacing wide;
    wide.before = -5;
    wide.after = 999999;
    wide.rule = LineRule::Exactly;
    wide.value = 999999;
    const Spacing clamped = clamp_spacing(wide);
    CHECK(clamped.before == 0);
    CHECK(clamped.after == kMaxIndent);
    CHECK(clamped.rule == LineRule::Exactly && clamped.value == kMaxIndent);
    Spacing named;
    named.rule = LineRule::Single;
    named.value = 10;
    CHECK(clamp_spacing(named).value == 240);
  }

  {
    SpacingFields fields;
    fields.units = Units::Inches;
    fields.before = "nope";
    fields.after = "0\"";
    fields.rule = LineRule::Single;
    SpacingCheck bad = check_spacing(fields);
    CHECK(bad.field == SpacingField::Before);
    CHECK(bad.message.find("not a valid measurement") != std::string::npos);

    fields.before = "23\"";
    bad = check_spacing(fields);
    CHECK(bad.field == SpacingField::Before);
    CHECK(bad.message.find("0\"") != std::string::npos && bad.message.find("22\"") != std::string::npos);

    fields.before = "0\"";
    fields.after = "x";
    bad = check_spacing(fields);
    CHECK(bad.field == SpacingField::After);

    fields.after = "0\"";
    fields.rule = LineRule::Exactly;
    fields.at = "nope";
    bad = check_spacing(fields);
    CHECK(bad.field == SpacingField::At);
    CHECK(bad.message.find("0 pt") != std::string::npos && bad.message.find("1584 pt") != std::string::npos);

    fields.at = "2000 pt";
    bad = check_spacing(fields);
    CHECK(bad.field == SpacingField::At);
    CHECK(bad.message.find("not a valid") == std::string::npos);

    fields.at = "12 pt";
    fields.current.rule = LineRule::Exactly;
    fields.current.value = 240;
    fields.at_shown = 12;
    SpacingCheck ok = check_spacing(fields);
    CHECK(ok.field == SpacingField::None);
    CHECK(ok.spacing.rule == LineRule::Exactly && ok.spacing.value == 240);

    fields.rule = LineRule::Multiple;
    fields.current.rule = LineRule::Multiple;
    fields.at = "3";
    fields.at_shown = 3;
    fields.current.value = 720;
    ok = check_spacing(fields);
    CHECK(ok.spacing.rule == LineRule::Multiple && ok.spacing.value == 720);

    fields.at = "1";
    fields.at_shown = 0;
    fields.current.value = 0;
    ok = check_spacing(fields);
    CHECK(ok.spacing.rule == LineRule::Single);

    fields.at = "1.5";
    ok = check_spacing(fields);
    CHECK(ok.spacing.rule == LineRule::OneHalf);

    fields.at = "2 lines";
    ok = check_spacing(fields);
    CHECK(ok.spacing.rule == LineRule::Double);

    fields.at = "nope";
    bad = check_spacing(fields);
    CHECK(bad.field == SpacingField::At);
    CHECK(bad.message.find("not a valid number") != std::string::npos);

    fields.at = "200";
    bad = check_spacing(fields);
    CHECK(bad.field == SpacingField::At);
    CHECK(bad.message.find("0 and 132") != std::string::npos);

    fields.current.before = 241;
    fields.before_shown = units_round(twips_to_units(241, Units::Inches), Units::Inches);
    fields.before = format_measure(fields.before_shown, Units::Inches);
    fields.after = "0\"";
    fields.after_shown = 0;
    fields.rule = LineRule::Single;
    fields.at = "11";
    ok = check_spacing(fields);
    CHECK(ok.field == SpacingField::None);
    CHECK(ok.spacing.before == 241);
  }

  {
    const std::string rtf =
        std::string(kHead) +
        "{\\stylesheet{\\s0\\snext0 Normal;}{\\s1\\sbasedon0\\snext1\\sb200\\sa100\\sl480\\slmult1 "
        "Spaced;}}\\pard\\plain\\s1 one\\par\\pard\\plain\\s1\\sb400 two\\par}";
    Document doc;
    CHECK(rtf_import(rtf, doc));
    CHECK(doc.paragraphs.size() == 2);
    const Style* style = find_style(style_sheet(doc), "Spaced");
    CHECK(style && style->spacing.before == 200 && style->spacing.after == 100 &&
          style->spacing.rule == LineRule::Double);
    CHECK(doc.paragraphs[0].spacing.before == 200 && doc.paragraphs[0].spacing.rule == LineRule::Double &&
          doc.paragraphs[0].direct == 0);
    CHECK(doc.paragraphs[1].spacing.before == 400 && doc.paragraphs[1].spacing.after == 100 &&
          doc.paragraphs[1].direct == 0);
  }

  {
    Document doc = blank_document("Sans", 11);
    Paragraph paragraph;
    paragraph.cell.table = 1;
    paragraph.cell.rows = 1;
    paragraph.cell.columns = 1;
    paragraph.cell.widths = {5000};
    paragraph.spacing = space(100, 200, LineRule::AtLeast, 280);
    paragraph.runs.push_back(text_run("A"));
    doc.paragraphs = {paragraph};
    Document back;
    CHECK(rtf_import(rtf_export(doc), back));
    CHECK(back.paragraphs.size() == 1 && back.paragraphs[0].cell.table != 0 &&
          back.paragraphs[0].spacing == paragraph.spacing);
  }

  {
    Document doc = blank_document("Sans", 11);
    Paragraph header;
    header.spacing = space(240, 120, LineRule::Double, 240);
    header.runs.push_back(text_run("Head"));
    doc.header.push_back(header);
    Document back;
    CHECK(rtf_import(rtf_export(doc), back));
    CHECK(back.header.size() == 1 && back.header[0].spacing.rule == LineRule::Double &&
          back.header[0].spacing.before == 240 && back.header[0].spacing.after == 120);
  }

  return suite_test::done("spacing", kChecks);
}
