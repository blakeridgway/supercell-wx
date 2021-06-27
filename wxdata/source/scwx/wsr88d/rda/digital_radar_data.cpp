#include <scwx/wsr88d/rda/digital_radar_data.hpp>

#include <boost/log/trivial.hpp>

namespace scwx
{
namespace wsr88d
{
namespace rda
{

static const std::string logPrefix_ =
   "[scwx::wsr88d::rda::digital_radar_data] ";

enum class DataBlockType
{
   Volume,
   Elevation,
   Radial,
   MomentRef,
   MomentVel,
   MomentSw,
   MomentZdr,
   MomentPhi,
   MomentRho,
   MomentCfp,
   Unknown
};

static const std::unordered_map<std::string, DataBlockType> strToDataBlock_ {
   {"VOL", DataBlockType::Volume},
   {"ELV", DataBlockType::Elevation},
   {"RAD", DataBlockType::Radial},
   {"REF", DataBlockType::MomentRef},
   {"VEL", DataBlockType::MomentVel},
   {"SW ", DataBlockType::MomentSw},
   {"ZDR", DataBlockType::MomentZdr},
   {"PHI", DataBlockType::MomentPhi},
   {"RHO", DataBlockType::MomentRho},
   {"CFP", DataBlockType::MomentCfp}};

struct DataBlock
{
   explicit DataBlock(const std::string& dataBlockType,
                      const std::string& dataName) :
       dataBlockType_ {dataBlockType}, dataName_ {dataName}
   {
   }

   std::string dataBlockType_;
   std::string dataName_;
};

struct MomentDataBlock : DataBlock
{
   explicit MomentDataBlock(const std::string& dataBlockType,
                            const std::string& dataName) :
       DataBlock(dataBlockType, dataName),
       numberOfDataMomentGates_ {0},
       dataMomentRange_ {0},
       dataMomentRangeSampleInterval_ {0},
       tover_ {0},
       snrThreshold_ {0},
       controlFlags_ {0},
       dataWordSize_ {0},
       scale_ {0.0f},
       offset_ {0.0f}
   {
   }

   uint16_t numberOfDataMomentGates_;
   uint16_t dataMomentRange_;
   uint16_t dataMomentRangeSampleInterval_;
   uint16_t tover_;
   int16_t  snrThreshold_;
   uint8_t  controlFlags_;
   uint8_t  dataWordSize_;
   float    scale_;
   float    offset_;

   std::vector<char>     momentGates8_;
   std::vector<uint16_t> momentGates16_;

   static std::unique_ptr<MomentDataBlock>
   Create(const std::string& dataBlockType,
          const std::string& dataName,
          std::istream&      is)
   {
      std::unique_ptr<MomentDataBlock> p =
         std::make_unique<MomentDataBlock>(dataBlockType, dataName);

      is.seekg(4, std::ios_base::cur);                                   // 4-7
      is.read(reinterpret_cast<char*>(&p->numberOfDataMomentGates_), 2); // 8-9
      is.read(reinterpret_cast<char*>(&p->dataMomentRange_), 2); // 10-11
      is.read(reinterpret_cast<char*>(&p->dataMomentRangeSampleInterval_),
              2);                                             // 12-13
      is.read(reinterpret_cast<char*>(&p->tover_), 2);        // 14-15
      is.read(reinterpret_cast<char*>(&p->snrThreshold_), 2); // 16-17
      is.read(reinterpret_cast<char*>(&p->controlFlags_), 1); // 18
      is.read(reinterpret_cast<char*>(&p->dataWordSize_), 1); // 19
      is.read(reinterpret_cast<char*>(&p->scale_), 4);        // 20-23
      is.read(reinterpret_cast<char*>(&p->offset_), 4);       // 24-27

      p->numberOfDataMomentGates_ = ntohs(p->numberOfDataMomentGates_);
      p->dataMomentRange_         = ntohs(p->dataMomentRange_);
      p->dataMomentRangeSampleInterval_ =
         ntohs(p->dataMomentRangeSampleInterval_);
      p->tover_        = ntohs(p->tover_);
      p->snrThreshold_ = ntohs(p->snrThreshold_);
      p->scale_        = Message::SwapFloat(p->scale_);
      p->offset_       = Message::SwapFloat(p->offset_);

      if (p->numberOfDataMomentGates_ >= 0 &&
          p->numberOfDataMomentGates_ <= 1840)
      {
         if (p->dataWordSize_ == 8)
         {
            p->momentGates8_.resize(p->numberOfDataMomentGates_);
            is.read(p->momentGates8_.data(), p->numberOfDataMomentGates_);
         }
         else if (p->dataWordSize_ == 16)
         {
            p->momentGates16_.resize(p->numberOfDataMomentGates_);
            is.read(reinterpret_cast<char*>(p->momentGates16_.data()),
                    p->numberOfDataMomentGates_ * 2);
            Message::SwapVector(p->momentGates16_);
         }
         else
         {
            BOOST_LOG_TRIVIAL(warning)
               << logPrefix_ << "Invalid data word size: " << p->dataWordSize_;
         }
      }
      else
      {
         BOOST_LOG_TRIVIAL(warning)
            << logPrefix_ << "Invalid number of data moment gates: "
            << p->numberOfDataMomentGates_;
      }

      return p;
   }
};

struct VolumeDataBlock : DataBlock
{
   explicit VolumeDataBlock(const std::string& dataBlockType,
                            const std::string& dataName) :
       DataBlock(dataBlockType, dataName),
       lrtup_ {0},
       versionNumberMajor_ {0},
       versionNumberMinor_ {0},
       latitude_ {0.0f},
       longitude_ {0.0f},
       siteHeight_ {0},
       feedhornHeight_ {0},
       calibrationConstant_ {0.0f},
       horizontaShvTxPower_ {0.0f},
       verticalShvTxPower_ {0.0f},
       systemDifferentialReflectivity_ {0.0f},
       initialSystemDifferentialPhase_ {0.0f},
       volumeCoveragePatternNumber_ {0},
       processingStatus_ {0}
   {
   }

   uint16_t lrtup_;
   uint8_t  versionNumberMajor_;
   uint8_t  versionNumberMinor_;
   float    latitude_;
   float    longitude_;
   int16_t  siteHeight_;
   uint16_t feedhornHeight_;
   float    calibrationConstant_;
   float    horizontaShvTxPower_;
   float    verticalShvTxPower_;
   float    systemDifferentialReflectivity_;
   float    initialSystemDifferentialPhase_;
   uint16_t volumeCoveragePatternNumber_;
   uint16_t processingStatus_;

   static std::unique_ptr<VolumeDataBlock>
   Create(const std::string& dataBlockType,
          const std::string& dataName,
          std::istream&      is)
   {
      std::unique_ptr<VolumeDataBlock> p =
         std::make_unique<VolumeDataBlock>(dataBlockType, dataName);

      is.read(reinterpret_cast<char*>(&p->lrtup_), 2);               // 4-5
      is.read(reinterpret_cast<char*>(&p->versionNumberMajor_), 1);  // 6
      is.read(reinterpret_cast<char*>(&p->versionNumberMinor_), 1);  // 7
      is.read(reinterpret_cast<char*>(&p->latitude_), 4);            // 8-11
      is.read(reinterpret_cast<char*>(&p->longitude_), 4);           // 12-15
      is.read(reinterpret_cast<char*>(&p->siteHeight_), 2);          // 16-17
      is.read(reinterpret_cast<char*>(&p->feedhornHeight_), 2);      // 18-19
      is.read(reinterpret_cast<char*>(&p->calibrationConstant_), 4); // 20-23
      is.read(reinterpret_cast<char*>(&p->horizontaShvTxPower_), 4); // 24-27
      is.read(reinterpret_cast<char*>(&p->verticalShvTxPower_), 4);  // 28-31
      is.read(reinterpret_cast<char*>(&p->systemDifferentialReflectivity_),
              4); // 32-35
      is.read(reinterpret_cast<char*>(&p->initialSystemDifferentialPhase_),
              4); // 36-39
      is.read(reinterpret_cast<char*>(&p->volumeCoveragePatternNumber_),
              2);                                                 // 40-41
      is.read(reinterpret_cast<char*>(&p->processingStatus_), 2); // 42-43

      p->lrtup_               = ntohs(p->lrtup_);
      p->latitude_            = Message::SwapFloat(p->latitude_);
      p->longitude_           = Message::SwapFloat(p->longitude_);
      p->siteHeight_          = ntohs(p->siteHeight_);
      p->feedhornHeight_      = ntohs(p->feedhornHeight_);
      p->calibrationConstant_ = Message::SwapFloat(p->calibrationConstant_);
      p->horizontaShvTxPower_ = Message::SwapFloat(p->horizontaShvTxPower_);
      p->verticalShvTxPower_  = Message::SwapFloat(p->verticalShvTxPower_);
      p->systemDifferentialReflectivity_ =
         Message::SwapFloat(p->systemDifferentialReflectivity_);
      p->initialSystemDifferentialPhase_ =
         Message::SwapFloat(p->initialSystemDifferentialPhase_);
      p->volumeCoveragePatternNumber_ = ntohs(p->volumeCoveragePatternNumber_);
      p->processingStatus_            = ntohs(p->processingStatus_);

      return p;
   }
};

struct ElevationDataBlock : DataBlock
{
   explicit ElevationDataBlock(const std::string& dataBlockType,
                               const std::string& dataName) :
       DataBlock(dataBlockType, dataName),
       lrtup_ {0},
       atmos_ {0},
       calibrationConstant_ {0.0f}
   {
   }

   uint16_t lrtup_;
   int16_t  atmos_;
   float    calibrationConstant_;

   static std::unique_ptr<ElevationDataBlock>
   Create(const std::string& dataBlockType,
          const std::string& dataName,
          std::istream&      is)
   {
      std::unique_ptr<ElevationDataBlock> p =
         std::make_unique<ElevationDataBlock>(dataBlockType, dataName);

      is.read(reinterpret_cast<char*>(&p->lrtup_), 2);               // 4-5
      is.read(reinterpret_cast<char*>(&p->atmos_), 2);               // 6-7
      is.read(reinterpret_cast<char*>(&p->calibrationConstant_), 4); // 8-11

      p->lrtup_               = ntohs(p->lrtup_);
      p->atmos_               = ntohs(p->atmos_);
      p->calibrationConstant_ = Message::SwapFloat(p->calibrationConstant_);

      return p;
   }
};

struct RadialDataBlock : DataBlock
{
   explicit RadialDataBlock(const std::string& dataBlockType,
                            const std::string& dataName) :
       DataBlock(dataBlockType, dataName),
       lrtup_ {0},
       unambigiousRange_ {0},
       noiseLevelHorizontal_ {0.0f},
       noiseLevelVertical_ {0.0f},
       nyquistVelocity_ {0},
       radialFlags_ {0},
       calibrationConstantHorizontal_ {0.0f},
       calibrationConstantVertical_ {0.0f}
   {
   }

   uint16_t lrtup_;
   uint16_t unambigiousRange_;
   float    noiseLevelHorizontal_;
   float    noiseLevelVertical_;
   uint16_t nyquistVelocity_;
   uint16_t radialFlags_;
   float    calibrationConstantHorizontal_;
   float    calibrationConstantVertical_;

   static std::unique_ptr<RadialDataBlock>
   Create(const std::string& dataBlockType,
          const std::string& dataName,
          std::istream&      is)
   {
      std::unique_ptr<RadialDataBlock> p =
         std::make_unique<RadialDataBlock>(dataBlockType, dataName);

      is.read(reinterpret_cast<char*>(&p->lrtup_), 2);                // 4-5
      is.read(reinterpret_cast<char*>(&p->unambigiousRange_), 2);     // 6-7
      is.read(reinterpret_cast<char*>(&p->noiseLevelHorizontal_), 4); // 8-11
      is.read(reinterpret_cast<char*>(&p->noiseLevelVertical_), 4);   // 12-15
      is.read(reinterpret_cast<char*>(&p->nyquistVelocity_), 2);      // 16-17
      is.read(reinterpret_cast<char*>(&p->radialFlags_), 2);          // 18-19
      is.read(reinterpret_cast<char*>(&p->calibrationConstantHorizontal_),
              4); // 20-23
      is.read(reinterpret_cast<char*>(&p->calibrationConstantVertical_),
              4); // 24-27

      p->lrtup_                = ntohs(p->lrtup_);
      p->unambigiousRange_     = ntohs(p->unambigiousRange_);
      p->noiseLevelHorizontal_ = Message::SwapFloat(p->noiseLevelHorizontal_);
      p->noiseLevelVertical_   = Message::SwapFloat(p->noiseLevelVertical_);
      p->nyquistVelocity_      = ntohs(p->nyquistVelocity_);
      p->radialFlags_          = ntohs(p->radialFlags_);
      p->calibrationConstantHorizontal_ =
         Message::SwapFloat(p->calibrationConstantHorizontal_);
      p->calibrationConstantVertical_ =
         Message::SwapFloat(p->calibrationConstantVertical_);

      return p;
   }
};

class DigitalRadarDataImpl
{
public:
   explicit DigitalRadarDataImpl() :
       radarIdentifier_ {},
       collectionTime_ {0},
       modifiedJulianDate_ {0},
       azimuthNumber_ {0},
       azimuthAngle_ {0.0f},
       compressionIndicator_ {0},
       radialLength_ {0},
       azimuthResolutionSpacing_ {0},
       radialStatus_ {0},
       elevationNumber_ {0},
       cutSectorNumber_ {0},
       elevationAngle_ {0.0f},
       radialSpotBlankingStatus_ {0},
       azimuthIndexingMode_ {0},
       dataBlockCount_ {0},
       dataBlockPointer_ {0},
       volumeDataBlock_ {nullptr},
       elevationDataBlock_ {nullptr},
       radialDataBlock_ {nullptr},
       momentRefDataBlock_ {nullptr},
       momentVelDataBlock_ {nullptr},
       momentSwDataBlock_ {nullptr},
       momentZdrDataBlock_ {nullptr},
       momentPhiDataBlock_ {nullptr},
       momentRhoDataBlock_ {nullptr},
       momentCfpDataBlock_ {nullptr} {};
   ~DigitalRadarDataImpl() = default;

   std::string              radarIdentifier_;
   uint32_t                 collectionTime_;
   uint16_t                 modifiedJulianDate_;
   uint16_t                 azimuthNumber_;
   float                    azimuthAngle_;
   uint8_t                  compressionIndicator_;
   uint16_t                 radialLength_;
   uint8_t                  azimuthResolutionSpacing_;
   uint8_t                  radialStatus_;
   uint8_t                  elevationNumber_;
   uint8_t                  cutSectorNumber_;
   float                    elevationAngle_;
   uint8_t                  radialSpotBlankingStatus_;
   uint8_t                  azimuthIndexingMode_;
   uint16_t                 dataBlockCount_;
   std::array<uint32_t, 10> dataBlockPointer_;

   std::unique_ptr<VolumeDataBlock>    volumeDataBlock_;
   std::unique_ptr<ElevationDataBlock> elevationDataBlock_;
   std::unique_ptr<RadialDataBlock>    radialDataBlock_;
   std::unique_ptr<MomentDataBlock>    momentRefDataBlock_;
   std::unique_ptr<MomentDataBlock>    momentVelDataBlock_;
   std::unique_ptr<MomentDataBlock>    momentSwDataBlock_;
   std::unique_ptr<MomentDataBlock>    momentZdrDataBlock_;
   std::unique_ptr<MomentDataBlock>    momentPhiDataBlock_;
   std::unique_ptr<MomentDataBlock>    momentRhoDataBlock_;
   std::unique_ptr<MomentDataBlock>    momentCfpDataBlock_;
};

DigitalRadarData::DigitalRadarData() :
    Message(), p(std::make_unique<DigitalRadarDataImpl>())
{
}
DigitalRadarData::~DigitalRadarData() = default;

DigitalRadarData::DigitalRadarData(DigitalRadarData&&) noexcept = default;
DigitalRadarData&
DigitalRadarData::operator=(DigitalRadarData&&) noexcept = default;

const std::string& DigitalRadarData::radar_identifier() const
{
   return p->radarIdentifier_;
}

uint32_t DigitalRadarData::collection_time() const
{
   return p->collectionTime_;
}

uint16_t DigitalRadarData::modified_julian_date() const
{
   return p->modifiedJulianDate_;
}

uint16_t DigitalRadarData::azimuth_number() const
{
   return p->azimuthNumber_;
}

float DigitalRadarData::azimuth_angle() const
{
   return p->azimuthAngle_;
}

uint8_t DigitalRadarData::compression_indicator() const
{
   return p->compressionIndicator_;
}

uint16_t DigitalRadarData::radial_length() const
{
   return p->radialLength_;
}

uint8_t DigitalRadarData::azimuth_resolution_spacing() const
{
   return p->azimuthResolutionSpacing_;
}

uint8_t DigitalRadarData::radial_status() const
{
   return p->radialStatus_;
}

uint8_t DigitalRadarData::elevation_number() const
{
   return p->elevationNumber_;
}

uint8_t DigitalRadarData::cut_sector_number() const
{
   return p->cutSectorNumber_;
}

float DigitalRadarData::elevation_angle() const
{
   return p->elevationAngle_;
}

uint8_t DigitalRadarData::radial_spot_blanking_status() const
{
   return p->radialSpotBlankingStatus_;
}

uint8_t DigitalRadarData::azimuth_indexing_mode() const
{
   return p->azimuthIndexingMode_;
}

uint16_t DigitalRadarData::data_block_count() const
{
   return p->dataBlockCount_;
}

bool DigitalRadarData::Parse(std::istream& is)
{
   BOOST_LOG_TRIVIAL(debug)
      << logPrefix_ << "Parsing Digital Radar Data (Message Type 31)";

   bool   messageValid = true;
   size_t bytesRead    = 0;

   std::streampos isBegin = is.tellg();

   p->radarIdentifier_.resize(4);

   is.read(&p->radarIdentifier_[0], 4);                                // 0-3
   is.read(reinterpret_cast<char*>(&p->collectionTime_), 4);           // 4-7
   is.read(reinterpret_cast<char*>(&p->modifiedJulianDate_), 2);       // 8-9
   is.read(reinterpret_cast<char*>(&p->azimuthNumber_), 2);            // 10-11
   is.read(reinterpret_cast<char*>(&p->azimuthAngle_), 4);             // 12-15
   is.read(reinterpret_cast<char*>(&p->compressionIndicator_), 1);     // 16
   is.seekg(1, std::ios_base::cur);                                    // 17
   is.read(reinterpret_cast<char*>(&p->radialLength_), 2);             // 18-19
   is.read(reinterpret_cast<char*>(&p->azimuthResolutionSpacing_), 1); // 20
   is.read(reinterpret_cast<char*>(&p->radialStatus_), 1);             // 21
   is.read(reinterpret_cast<char*>(&p->elevationNumber_), 1);          // 22
   is.read(reinterpret_cast<char*>(&p->cutSectorNumber_), 1);          // 23
   is.read(reinterpret_cast<char*>(&p->elevationAngle_), 4);           // 24-27
   is.read(reinterpret_cast<char*>(&p->radialSpotBlankingStatus_), 1); // 28
   is.read(reinterpret_cast<char*>(&p->azimuthIndexingMode_), 1);      // 29
   is.read(reinterpret_cast<char*>(&p->dataBlockCount_), 2);           // 30-31

   p->collectionTime_     = ntohl(p->collectionTime_);
   p->modifiedJulianDate_ = ntohs(p->modifiedJulianDate_);
   p->azimuthNumber_      = ntohs(p->azimuthNumber_);
   p->azimuthAngle_       = SwapFloat(p->azimuthAngle_);
   p->radialLength_       = ntohs(p->radialLength_);
   p->elevationAngle_     = SwapFloat(p->elevationAngle_);
   p->dataBlockCount_     = ntohs(p->dataBlockCount_);

   if (p->dataBlockCount_ < 4 || p->dataBlockCount_ > 10)
   {
      BOOST_LOG_TRIVIAL(warning)
         << logPrefix_
         << "Invalid number of data blocks: " << p->dataBlockCount_;
      p->dataBlockCount_ = 0;
      messageValid       = false;
   }
   if (p->compressionIndicator_ != 0)
   {
      BOOST_LOG_TRIVIAL(warning) << logPrefix_ << "Compression not supported";
      p->dataBlockCount_ = 0;
      messageValid       = false;
   }

   is.read(reinterpret_cast<char*>(&p->dataBlockPointer_),
           p->dataBlockCount_ * 4);

   SwapArray(p->dataBlockPointer_, p->dataBlockCount_);

   for (uint16_t b = 0; b < p->dataBlockCount_; ++b)
   {
      is.seekg(isBegin + std::streamoff(p->dataBlockPointer_[b]),
               std::ios_base::beg);

      std::string dataBlockType(1, 0);
      std::string dataName(3, 0);

      is.read(&dataBlockType[0], 1);
      is.read(&dataName[0], 3);

      DataBlockType dataBlock = DataBlockType::Unknown;
      try
      {
         dataBlock = strToDataBlock_.at(dataName);
      }
      catch (const std::exception&)
      {
      }

      switch (dataBlock)
      {
      case DataBlockType::Volume:
         p->volumeDataBlock_ =
            std::move(VolumeDataBlock::Create(dataBlockType, dataName, is));
         break;
      case DataBlockType::Elevation:
         p->elevationDataBlock_ =
            std::move(ElevationDataBlock::Create(dataBlockType, dataName, is));
         break;
      case DataBlockType::Radial:
         p->radialDataBlock_ =
            std::move(RadialDataBlock::Create(dataBlockType, dataName, is));
         break;
      case DataBlockType::MomentRef:
         p->momentRefDataBlock_ =
            std::move(MomentDataBlock::Create(dataBlockType, dataName, is));
         break;
      case DataBlockType::MomentVel:
         p->momentVelDataBlock_ =
            std::move(MomentDataBlock::Create(dataBlockType, dataName, is));
         break;
      case DataBlockType::MomentSw:
         p->momentSwDataBlock_ =
            std::move(MomentDataBlock::Create(dataBlockType, dataName, is));
         break;
      case DataBlockType::MomentZdr:
         p->momentZdrDataBlock_ =
            std::move(MomentDataBlock::Create(dataBlockType, dataName, is));
         break;
      case DataBlockType::MomentPhi:
         p->momentPhiDataBlock_ =
            std::move(MomentDataBlock::Create(dataBlockType, dataName, is));
         break;
      case DataBlockType::MomentRho:
         p->momentRhoDataBlock_ =
            std::move(MomentDataBlock::Create(dataBlockType, dataName, is));
         break;
      case DataBlockType::MomentCfp:
         p->momentCfpDataBlock_ =
            std::move(MomentDataBlock::Create(dataBlockType, dataName, is));
         break;
      default:
         BOOST_LOG_TRIVIAL(warning)
            << logPrefix_ << "Unknown data name: " << dataName;
         break;
      }
   }

   is.seekg(isBegin, std::ios_base::beg);
   if (!ValidateMessage(is, bytesRead))
   {
      messageValid = false;
   }

   return messageValid;
}

std::unique_ptr<DigitalRadarData>
DigitalRadarData::Create(MessageHeader&& header, std::istream& is)
{
   std::unique_ptr<DigitalRadarData> message =
      std::make_unique<DigitalRadarData>();
   message->set_header(std::move(header));
   message->Parse(is);
   return message;
}

} // namespace rda
} // namespace wsr88d
} // namespace scwx