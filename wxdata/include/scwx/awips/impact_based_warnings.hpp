#pragma once

#include <string>

namespace scwx
{
namespace awips
{

enum class ThreatCategory : int
{
   Base         = 0,
   Significant  = 1,
   Considerable = 2,
   Destructive  = 3,
   Catastrophic = 4,
   Unknown
};

ThreatCategory     GetThreatCategory(const std::string& name);
const std::string& GetThreatCategoryName(ThreatCategory threatCategory);

} // namespace awips
} // namespace scwx
