/* SPDX-License-Identifier: Unlicense */

#include "para_check.hpp"

namespace writeit {

double max_measure(Units)
{
  return 0;
}

MeasureCheck check_measure(const std::string&, Units, double&)
{
  return MeasureCheck::Ok;
}

std::string measure_message(MeasureCheck, Units)
{
  return "";
}

ParaCheck check_paragraph(const ParaFields&, int)
{
  return ParaCheck{};
}

}  // namespace writeit
