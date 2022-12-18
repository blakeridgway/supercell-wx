#include <scwx/qt/settings/general_settings.hpp>
#include <scwx/qt/util/json.hpp>
#include <scwx/util/logger.hpp>

#include <scwx/qt/settings/settings_container.hpp>

namespace scwx
{
namespace qt
{
namespace settings
{

static const std::string logPrefix_ = "scwx::qt::settings::general_settings";
static const auto        logger_    = scwx::util::Logger::Create(logPrefix_);

class GeneralSettingsImpl
{
public:
   explicit GeneralSettingsImpl()
   {
      debugEnabled_.SetDefault(false);
      defaultRadarSite_.SetDefault("KLSX");
      fontSizes_.SetDefault({16});
      gridWidth_.SetDefault(1);
      gridHeight_.SetDefault(1);
      mapboxApiKey_.SetDefault("?");

      fontSizes_.SetElementMinimum(1);
      fontSizes_.SetElementMaximum(72);
      fontSizes_.SetValidator([](const std::vector<std::int64_t>& value)
                              { return !value.empty(); });
      gridWidth_.SetMinimum(1);
      gridWidth_.SetMaximum(2);
      gridHeight_.SetMinimum(1);
      gridHeight_.SetMaximum(2);
      mapboxApiKey_.SetValidator([](const std::string& value)
                                 { return !value.empty(); });
   }

   ~GeneralSettingsImpl() {}

   SettingsVariable<bool>        debugEnabled_ {"debug_enabled"};
   SettingsVariable<std::string> defaultRadarSite_ {"default_radar_site"};
   SettingsContainer<std::vector<std::int64_t>> fontSizes_ {"font_sizes"};
   SettingsVariable<std::int64_t>               gridWidth_ {"grid_width"};
   SettingsVariable<std::int64_t>               gridHeight_ {"grid_height"};
   SettingsVariable<std::string> mapboxApiKey_ {"mapbox_api_key"};
};

GeneralSettings::GeneralSettings() :
    SettingsCategory("general"), p(std::make_unique<GeneralSettingsImpl>())
{
   RegisterVariables({&p->debugEnabled_,
                      &p->defaultRadarSite_,
                      &p->fontSizes_,
                      &p->gridWidth_,
                      &p->gridHeight_,
                      &p->mapboxApiKey_});
   SetDefaults();
}
GeneralSettings::~GeneralSettings() = default;

GeneralSettings::GeneralSettings(GeneralSettings&&) noexcept = default;
GeneralSettings&
GeneralSettings::operator=(GeneralSettings&&) noexcept = default;

bool GeneralSettings::debug_enabled() const
{
   return p->debugEnabled_.GetValue();
}

std::string GeneralSettings::default_radar_site() const
{
   return p->defaultRadarSite_.GetValue();
}

std::vector<std::int64_t> GeneralSettings::font_sizes() const
{
   return p->fontSizes_.GetValue();
}

std::int64_t GeneralSettings::grid_height() const
{
   return p->gridHeight_.GetValue();
}

std::int64_t GeneralSettings::grid_width() const
{
   return p->gridWidth_.GetValue();
}

std::string GeneralSettings::mapbox_api_key() const
{
   return p->mapboxApiKey_.GetValue();
}

bool operator==(const GeneralSettings& lhs, const GeneralSettings& rhs)
{
   return (lhs.p->debugEnabled_ == rhs.p->debugEnabled_ &&
           lhs.p->defaultRadarSite_ == rhs.p->defaultRadarSite_ &&
           lhs.p->fontSizes_ == rhs.p->fontSizes_ &&
           lhs.p->gridWidth_ == rhs.p->gridWidth_ &&
           lhs.p->gridHeight_ == rhs.p->gridHeight_ &&
           lhs.p->mapboxApiKey_ == rhs.p->mapboxApiKey_);
}

} // namespace settings
} // namespace qt
} // namespace scwx
