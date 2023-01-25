#include <scwx/qt/manager/radar_product_manager.hpp>
#include <scwx/qt/manager/radar_product_manager_notifier.hpp>
#include <scwx/common/constants.hpp>
#include <scwx/provider/nexrad_data_provider_factory.hpp>
#include <scwx/util/logger.hpp>
#include <scwx/util/map.hpp>
#include <scwx/util/threads.hpp>
#include <scwx/util/time.hpp>
#include <scwx/wsr88d/nexrad_file_factory.hpp>

#include <deque>
#include <execution>
#include <mutex>
#include <shared_mutex>

#pragma warning(push, 0)
#include <boost/asio/steady_timer.hpp>
#include <boost/range/irange.hpp>
#include <boost/timer/timer.hpp>
#include <fmt/chrono.h>
#include <GeographicLib/Geodesic.hpp>
#include <QMapLibreGL/QMapLibreGL>
#pragma warning(pop)

namespace scwx
{
namespace qt
{
namespace manager
{

static const std::string logPrefix_ =
   "scwx::qt::manager::radar_product_manager";
static const auto logger_ = scwx::util::Logger::Create(logPrefix_);

typedef std::function<std::shared_ptr<wsr88d::NexradFile>()>
   CreateNexradFileFunction;
typedef std::map<std::chrono::system_clock::time_point,
                 std::shared_ptr<types::RadarProductRecord>>
   RadarProductRecordMap;

static constexpr uint32_t NUM_RADIAL_GATES_0_5_DEGREE =
   common::MAX_0_5_DEGREE_RADIALS * common::MAX_DATA_MOMENT_GATES;
static constexpr uint32_t NUM_RADIAL_GATES_1_DEGREE =
   common::MAX_1_DEGREE_RADIALS * common::MAX_DATA_MOMENT_GATES;
static constexpr uint32_t NUM_COORIDNATES_0_5_DEGREE =
   NUM_RADIAL_GATES_0_5_DEGREE * 2;
static constexpr uint32_t NUM_COORIDNATES_1_DEGREE =
   NUM_RADIAL_GATES_1_DEGREE * 2;

static const std::string kDefaultLevel3Product_ {"N0B"};

static constexpr std::chrono::seconds kRetryInterval_ {15};

static std::unordered_map<std::string, std::weak_ptr<RadarProductManager>>
                  instanceMap_;
static std::mutex instanceMutex_;

static std::unordered_map<std::string,
                          std::shared_ptr<types::RadarProductRecord>>
                         fileIndex_;
static std::shared_mutex fileIndexMutex_;

static std::mutex fileLoadMutex_;

class ProviderManager : public QObject
{
   Q_OBJECT
public:
   explicit ProviderManager(RadarProductManager*      self,
                            const std::string&        radarId,
                            common::RadarProductGroup group) :
       ProviderManager(self, radarId, group, "???")
   {
   }
   explicit ProviderManager(RadarProductManager*      self,
                            const std::string&        radarId,
                            common::RadarProductGroup group,
                            const std::string&        product) :
       radarId_ {radarId},
       group_ {group},
       product_ {product},
       refreshEnabled_ {false},
       refreshTimer_ {util::io_context()},
       refreshTimerMutex_ {},
       provider_ {nullptr}
   {
      connect(this,
              &ProviderManager::NewDataAvailable,
              self,
              &RadarProductManager::NewDataAvailable);
   }
   ~ProviderManager() = default;

   std::string name() const;

   void Disable();

   const std::string                             radarId_;
   const common::RadarProductGroup               group_;
   const std::string                             product_;
   bool                                          refreshEnabled_;
   boost::asio::steady_timer                     refreshTimer_;
   std::mutex                                    refreshTimerMutex_;
   std::shared_ptr<provider::NexradDataProvider> provider_;

signals:
   void NewDataAvailable(common::RadarProductGroup             group,
                         const std::string&                    product,
                         std::chrono::system_clock::time_point latestTime);
};

class RadarProductManagerImpl
{
public:
   explicit RadarProductManagerImpl(RadarProductManager* self,
                                    const std::string&   radarId) :
       self_ {self},
       radarId_ {radarId},
       initialized_ {false},
       level3ProductsInitialized_ {false},
       radarSite_ {config::RadarSite::Get(radarId)},
       coordinates0_5Degree_ {},
       coordinates1Degree_ {},
       level2ProductRecords_ {},
       level3ProductRecordsMap_ {},
       level2ProductRecordMutex_ {},
       level3ProductRecordMutex_ {},
       level2ProviderManager_ {std::make_shared<ProviderManager>(
          self_, radarId_, common::RadarProductGroup::Level2)},
       level3ProviderManagerMap_ {},
       level3ProviderManagerMutex_ {},
       initializeMutex_ {},
       level3ProductsInitializeMutex_ {},
       loadLevel2DataMutex_ {},
       loadLevel3DataMutex_ {},
       availableCategoryMap_ {},
       availableCategoryMutex_ {}
   {
      if (radarSite_ == nullptr)
      {
         logger_->warn("Radar site not found: \"{}\"", radarId_);
         radarSite_ = std::make_shared<config::RadarSite>();
      }

      level2ProviderManager_->provider_ =
         provider::NexradDataProviderFactory::CreateLevel2DataProvider(radarId);
   }
   ~RadarProductManagerImpl()
   {
      level2ProviderManager_->Disable();

      std::shared_lock lock(level3ProviderManagerMutex_);
      std::for_each(std::execution::par_unseq,
                    level3ProviderManagerMap_.begin(),
                    level3ProviderManagerMap_.end(),
                    [](auto& p)
                    {
                       auto& [key, providerManager] = p;
                       providerManager->Disable();
                    });
   }

   RadarProductManager* self_;

   std::shared_ptr<ProviderManager>
   GetLevel3ProviderManager(const std::string& product);

   void EnableRefresh(std::shared_ptr<ProviderManager> providerManager,
                      bool                             enabled);
   void RefreshData(std::shared_ptr<ProviderManager> providerManager);

   std::shared_ptr<types::RadarProductRecord>
   GetLevel2ProductRecord(std::chrono::system_clock::time_point time);
   std::shared_ptr<types::RadarProductRecord>
   GetLevel3ProductRecord(const std::string&                    product,
                          std::chrono::system_clock::time_point time);
   std::shared_ptr<types::RadarProductRecord>
   StoreRadarProductRecord(std::shared_ptr<types::RadarProductRecord> record);

   void LoadProviderData(std::chrono::system_clock::time_point time,
                         std::shared_ptr<ProviderManager>      providerManager,
                         RadarProductRecordMap&                recordMap,
                         std::shared_mutex&                    recordMutex,
                         std::mutex&                           loadDataMutex,
                         std::shared_ptr<request::NexradFileRequest> request);

   static void
   LoadNexradFile(CreateNexradFileFunction                    load,
                  std::shared_ptr<request::NexradFileRequest> request,
                  std::mutex&                                 mutex);

   const std::string radarId_;
   bool              initialized_;
   bool              level3ProductsInitialized_;

   std::shared_ptr<config::RadarSite> radarSite_;

   std::vector<float> coordinates0_5Degree_;
   std::vector<float> coordinates1Degree_;

   RadarProductRecordMap level2ProductRecords_;
   std::unordered_map<std::string, RadarProductRecordMap>
      level3ProductRecordsMap_;

   std::shared_mutex level2ProductRecordMutex_;
   std::shared_mutex level3ProductRecordMutex_;

   std::shared_ptr<ProviderManager> level2ProviderManager_;
   std::unordered_map<std::string, std::shared_ptr<ProviderManager>>
                     level3ProviderManagerMap_;
   std::shared_mutex level3ProviderManagerMutex_;

   std::mutex initializeMutex_;
   std::mutex level3ProductsInitializeMutex_;
   std::mutex loadLevel2DataMutex_;
   std::mutex loadLevel3DataMutex_;

   common::Level3ProductCategoryMap availableCategoryMap_;
   std::shared_mutex                availableCategoryMutex_;
};

RadarProductManager::RadarProductManager(const std::string& radarId) :
    p(std::make_unique<RadarProductManagerImpl>(this, radarId))
{
}
RadarProductManager::~RadarProductManager() = default;

std::string ProviderManager::name() const
{
   std::string name;

   if (group_ == common::RadarProductGroup::Level3)
   {
      name = std::format("{}, {}, {}",
                         radarId_,
                         common::GetRadarProductGroupName(group_),
                         product_);
   }
   else
   {
      name = std::format(
         "{}, {}", radarId_, common::GetRadarProductGroupName(group_));
   }

   return name;
}

void ProviderManager::Disable()
{
   std::unique_lock lock(refreshTimerMutex_);
   refreshEnabled_ = false;
   refreshTimer_.cancel();
}

void RadarProductManager::Cleanup()
{
   {
      std::unique_lock lock(fileIndexMutex_);
      fileIndex_.clear();
   }

   {
      std::unique_lock lock(instanceMutex_);
      instanceMap_.clear();
   }
}

const std::vector<float>&
RadarProductManager::coordinates(common::RadialSize radialSize) const
{
   switch (radialSize)
   {
   case common::RadialSize::_0_5Degree:
      return p->coordinates0_5Degree_;
   case common::RadialSize::_1Degree:
      return p->coordinates1Degree_;
   }

   throw std::exception("Invalid radial size");
}

float RadarProductManager::gate_size() const
{
   return (p->radarSite_->type() == "tdwr") ? 150.0f : 250.0f;
}

std::shared_ptr<config::RadarSite> RadarProductManager::radar_site() const
{
   return p->radarSite_;
}

void RadarProductManager::Initialize()
{
   std::unique_lock lock {p->initializeMutex_};

   if (p->initialized_)
   {
      return;
   }

   logger_->debug("Initialize()");

   boost::timer::cpu_timer timer;

   GeographicLib::Geodesic geodesic(GeographicLib::Constants::WGS84_a(),
                                    GeographicLib::Constants::WGS84_f());

   const QMapLibreGL::Coordinate radar(p->radarSite_->latitude(),
                                       p->radarSite_->longitude());

   const float gateSize = gate_size();

   // Calculate half degree azimuth coordinates
   timer.start();
   std::vector<float>& coordinates0_5Degree = p->coordinates0_5Degree_;

   coordinates0_5Degree.resize(NUM_COORIDNATES_0_5_DEGREE);

   auto radialGates0_5Degree =
      boost::irange<uint32_t>(0, NUM_RADIAL_GATES_0_5_DEGREE);

   std::for_each(
      std::execution::par_unseq,
      radialGates0_5Degree.begin(),
      radialGates0_5Degree.end(),
      [&](uint32_t radialGate)
      {
         const uint16_t gate =
            static_cast<uint16_t>(radialGate % common::MAX_DATA_MOMENT_GATES);
         const uint16_t radial =
            static_cast<uint16_t>(radialGate / common::MAX_DATA_MOMENT_GATES);

         const float  angle  = radial * 0.5f - 0.25f; // 0.5 degree radial
         const float  range  = (gate + 1) * gateSize;
         const size_t offset = radialGate * 2;

         double latitude;
         double longitude;

         geodesic.Direct(
            radar.first, radar.second, angle, range, latitude, longitude);

         coordinates0_5Degree[offset]     = latitude;
         coordinates0_5Degree[offset + 1] = longitude;
      });
   timer.stop();
   logger_->debug("Coordinates (0.5 degree) calculated in {}",
                  timer.format(6, "%ws"));

   // Calculate 1 degree azimuth coordinates
   timer.start();
   std::vector<float>& coordinates1Degree = p->coordinates1Degree_;

   coordinates1Degree.resize(NUM_COORIDNATES_1_DEGREE);

   auto radialGates1Degree =
      boost::irange<uint32_t>(0, NUM_RADIAL_GATES_1_DEGREE);

   std::for_each(
      std::execution::par_unseq,
      radialGates1Degree.begin(),
      radialGates1Degree.end(),
      [&](uint32_t radialGate)
      {
         const uint16_t gate =
            static_cast<uint16_t>(radialGate % common::MAX_DATA_MOMENT_GATES);
         const uint16_t radial =
            static_cast<uint16_t>(radialGate / common::MAX_DATA_MOMENT_GATES);

         const float  angle  = radial * 1.0f - 0.5f; // 1 degree radial
         const float  range  = (gate + 1) * gateSize;
         const size_t offset = radialGate * 2;

         double latitude;
         double longitude;

         geodesic.Direct(
            radar.first, radar.second, angle, range, latitude, longitude);

         coordinates1Degree[offset]     = latitude;
         coordinates1Degree[offset + 1] = longitude;
      });
   timer.stop();
   logger_->debug("Coordinates (1 degree) calculated in {}",
                  timer.format(6, "%ws"));

   p->initialized_ = true;
}

std::shared_ptr<ProviderManager>
RadarProductManagerImpl::GetLevel3ProviderManager(const std::string& product)
{
   std::unique_lock lock(level3ProviderManagerMutex_);

   if (!level3ProviderManagerMap_.contains(product))
   {
      level3ProviderManagerMap_.emplace(
         std::piecewise_construct,
         std::forward_as_tuple(product),
         std::forward_as_tuple(std::make_shared<ProviderManager>(
            self_, radarId_, common::RadarProductGroup::Level3, product)));
      level3ProviderManagerMap_.at(product)->provider_ =
         provider::NexradDataProviderFactory::CreateLevel3DataProvider(radarId_,
                                                                       product);
   }

   std::shared_ptr<ProviderManager> providerManager =
      level3ProviderManagerMap_.at(product);

   return providerManager;
}

void RadarProductManager::EnableRefresh(common::RadarProductGroup group,
                                        const std::string&        product,
                                        bool                      enabled)
{
   if (group == common::RadarProductGroup::Level2)
   {
      p->EnableRefresh(p->level2ProviderManager_, enabled);
   }
   else
   {
      std::shared_ptr<ProviderManager> providerManager =
         p->GetLevel3ProviderManager(product);

      // Only enable refresh on available products
      util::async(
         [=]()
         {
            providerManager->provider_->RequestAvailableProducts();
            auto availableProducts =
               providerManager->provider_->GetAvailableProducts();

            if (std::find(std::execution::par_unseq,
                          availableProducts.cbegin(),
                          availableProducts.cend(),
                          product) != availableProducts.cend())
            {
               p->EnableRefresh(providerManager, enabled);
            }
         });
   }
}

void RadarProductManagerImpl::EnableRefresh(
   std::shared_ptr<ProviderManager> providerManager, bool enabled)
{
   if (providerManager->refreshEnabled_ != enabled)
   {
      providerManager->refreshEnabled_ = enabled;

      if (enabled)
      {
         RefreshData(providerManager);
      }
   }
}

void RadarProductManagerImpl::RefreshData(
   std::shared_ptr<ProviderManager> providerManager)
{
   logger_->debug("RefreshData: {}", providerManager->name());

   {
      std::unique_lock lock(providerManager->refreshTimerMutex_);
      providerManager->refreshTimer_.cancel();
   }

   util::async(
      [=]()
      {
         auto [newObjects, totalObjects] =
            providerManager->provider_->Refresh();

         std::chrono::milliseconds interval = kRetryInterval_;

         if (newObjects > 0)
         {
            std::string key = providerManager->provider_->FindLatestKey();
            auto        latestTime =
               providerManager->provider_->GetTimePointByKey(key);

            auto updatePeriod = providerManager->provider_->update_period();
            auto lastModified = providerManager->provider_->last_modified();
            interval = std::chrono::duration_cast<std::chrono::milliseconds>(
               updatePeriod -
               (std::chrono::system_clock::now() - lastModified));
            if (interval < std::chrono::milliseconds {kRetryInterval_})
            {
               interval = kRetryInterval_;
            }

            emit providerManager->NewDataAvailable(
               providerManager->group_, providerManager->product_, latestTime);
         }
         else if (providerManager->refreshEnabled_ && totalObjects == 0)
         {
            logger_->info("[{}] No data found, disabling refresh",
                          providerManager->name());

            providerManager->refreshEnabled_ = false;
         }

         if (providerManager->refreshEnabled_)
         {
            std::unique_lock lock(providerManager->refreshTimerMutex_);

            logger_->debug(
               "[{}] Scheduled refresh in {:%M:%S}",
               providerManager->name(),
               std::chrono::duration_cast<std::chrono::seconds>(interval));

            {
               providerManager->refreshTimer_.expires_after(interval);
               providerManager->refreshTimer_.async_wait(
                  [=](const boost::system::error_code& e)
                  {
                     if (e == boost::system::errc::success)
                     {
                        RefreshData(providerManager);
                     }
                     else if (e == boost::asio::error::operation_aborted)
                     {
                        logger_->debug("[{}] Data refresh timer cancelled",
                                       providerManager->name());
                     }
                     else
                     {
                        logger_->warn("[{}] Data refresh timer error: {}",
                                      providerManager->name(),
                                      e.message());
                     }
                  });
            }
         }
      });
}

void RadarProductManagerImpl::LoadProviderData(
   std::chrono::system_clock::time_point       time,
   std::shared_ptr<ProviderManager>            providerManager,
   RadarProductRecordMap&                      recordMap,
   std::shared_mutex&                          recordMutex,
   std::mutex&                                 loadDataMutex,
   std::shared_ptr<request::NexradFileRequest> request)
{
   logger_->debug("LoadProviderData: {}, {}",
                  providerManager->name(),
                  util::TimeString(time));

   RadarProductManagerImpl::LoadNexradFile(
      [=, &recordMap, &recordMutex, &loadDataMutex]()
         -> std::shared_ptr<wsr88d::NexradFile>
      {
         std::shared_ptr<types::RadarProductRecord> existingRecord = nullptr;
         std::shared_ptr<wsr88d::NexradFile>        nexradFile     = nullptr;

         {
            std::shared_lock sharedLock {recordMutex};

            auto it = recordMap.find(time);
            if (it != recordMap.cend())
            {
               logger_->debug(
                  "Data previously loaded, loading from data cache");

               existingRecord = it->second;
            }
         }

         if (existingRecord == nullptr)
         {
            std::string key = providerManager->provider_->FindKey(time);
            nexradFile      = providerManager->provider_->LoadObjectByKey(key);
         }
         else
         {
            nexradFile = existingRecord->nexrad_file();
         }

         return nexradFile;
      },
      request,
      loadDataMutex);
}

void RadarProductManager::LoadLevel2Data(
   std::chrono::system_clock::time_point       time,
   std::shared_ptr<request::NexradFileRequest> request)
{
   logger_->debug("LoadLevel2Data: {}", util::TimeString(time));

   p->LoadProviderData(time,
                       p->level2ProviderManager_,
                       p->level2ProductRecords_,
                       p->level2ProductRecordMutex_,
                       p->loadLevel2DataMutex_,
                       request);
}

void RadarProductManager::LoadLevel3Data(
   const std::string&                          product,
   std::chrono::system_clock::time_point       time,
   std::shared_ptr<request::NexradFileRequest> request)
{
   logger_->debug("LoadLevel3Data: {}", util::TimeString(time));

   // Look up provider manager
   std::shared_lock providerManagerLock(p->level3ProviderManagerMutex_);
   auto level3ProviderManager = p->level3ProviderManagerMap_.find(product);
   if (level3ProviderManager == p->level3ProviderManagerMap_.cend())
   {
      logger_->debug("No level 3 provider manager for product: {}", product);
      return;
   }
   providerManagerLock.unlock();

   // Look up product record
   std::unique_lock       productRecordLock(p->level3ProductRecordMutex_);
   RadarProductRecordMap& level3ProductRecords =
      p->level3ProductRecordsMap_[product];
   productRecordLock.unlock();

   // Load provider data
   p->LoadProviderData(time,
                       level3ProviderManager->second,
                       level3ProductRecords,
                       p->level3ProductRecordMutex_,
                       p->loadLevel3DataMutex_,
                       request);
}

void RadarProductManager::LoadData(
   std::istream& is, std::shared_ptr<request::NexradFileRequest> request)
{
   logger_->debug("LoadData()");

   RadarProductManagerImpl::LoadNexradFile(
      [=, &is]() -> std::shared_ptr<wsr88d::NexradFile>
      { return wsr88d::NexradFileFactory::Create(is); },
      request,
      fileLoadMutex_);
}

void RadarProductManager::LoadFile(
   const std::string&                          filename,
   std::shared_ptr<request::NexradFileRequest> request)
{
   logger_->debug("LoadFile: {}", filename);

   std::shared_ptr<types::RadarProductRecord> existingRecord = nullptr;

   {
      std::shared_lock lock {fileIndexMutex_};
      auto             it = fileIndex_.find(filename);
      if (it != fileIndex_.cend())
      {
         logger_->debug("File previously loaded, loading from file cache");

         existingRecord = it->second;
      }
   }

   if (existingRecord == nullptr)
   {
      QObject::connect(request.get(),
                       &request::NexradFileRequest::RequestComplete,
                       [=](std::shared_ptr<request::NexradFileRequest> request)
                       {
                          auto record = request->radar_product_record();

                          if (record != nullptr)
                          {
                             std::unique_lock lock {fileIndexMutex_};
                             fileIndex_[filename] = record;
                          }
                       });

      RadarProductManagerImpl::LoadNexradFile(
         [=]() -> std::shared_ptr<wsr88d::NexradFile>
         { return wsr88d::NexradFileFactory::Create(filename); },
         request,
         fileLoadMutex_);
   }
   else if (request != nullptr)
   {
      request->set_radar_product_record(existingRecord);
      emit request->RequestComplete(request);
   }
}

void RadarProductManagerImpl::LoadNexradFile(
   CreateNexradFileFunction                    load,
   std::shared_ptr<request::NexradFileRequest> request,
   std::mutex&                                 mutex)
{
   scwx::util::async(
      [=, &mutex]()
      {
         std::unique_lock lock {mutex};

         std::shared_ptr<wsr88d::NexradFile> nexradFile = load();

         std::shared_ptr<types::RadarProductRecord> record = nullptr;

         bool fileValid = (nexradFile != nullptr);

         if (fileValid)
         {
            record = types::RadarProductRecord::Create(nexradFile);

            std::shared_ptr<RadarProductManager> manager =
               RadarProductManager::Instance(record->radar_id());

            manager->Initialize();
            record = manager->p->StoreRadarProductRecord(record);
         }

         lock.unlock();

         if (request != nullptr)
         {
            request->set_radar_product_record(record);
            emit request->RequestComplete(request);
         }
      });
}

std::shared_ptr<types::RadarProductRecord>
RadarProductManagerImpl::GetLevel2ProductRecord(
   std::chrono::system_clock::time_point time)
{
   std::shared_ptr<types::RadarProductRecord> record;

   if (!level2ProductRecords_.empty() &&
       time == std::chrono::system_clock::time_point {})
   {
      // If a default-initialized time point is given, return the latest record
      record = level2ProductRecords_.rbegin()->second;
   }
   else
   {
      // TODO: Round to minutes
      record = util::GetBoundedElementValue(level2ProductRecords_, time);

      // Does the record contain the time we are looking for?
      if (record != nullptr && (time < record->level2_file()->start_time()))
      {
         record = nullptr;
      }
   }

   return record;
}

std::shared_ptr<types::RadarProductRecord>
RadarProductManagerImpl::GetLevel3ProductRecord(
   const std::string& product, std::chrono::system_clock::time_point time)
{
   std::shared_ptr<types::RadarProductRecord> record = nullptr;

   std::unique_lock lock {level3ProductRecordMutex_};

   auto it = level3ProductRecordsMap_.find(product);

   if (it != level3ProductRecordsMap_.cend())
   {
      if (time == std::chrono::system_clock::time_point {})
      {
         // If a default-initialized time point is given, return the latest
         // record
         record = it->second.rbegin()->second;
      }
      else
      {
         record = util::GetBoundedElementValue(it->second, time);
      }
   }

   return record;
}

std::shared_ptr<types::RadarProductRecord>
RadarProductManagerImpl::StoreRadarProductRecord(
   std::shared_ptr<types::RadarProductRecord> record)
{
   logger_->debug("StoreRadarProductRecord()");

   std::shared_ptr<types::RadarProductRecord> storedRecord = record;

   auto timeInSeconds =
      std::chrono::time_point_cast<std::chrono::seconds,
                                   std::chrono::system_clock>(record->time());

   if (record->radar_product_group() == common::RadarProductGroup::Level2)
   {
      std::unique_lock lock {level2ProductRecordMutex_};

      auto it = level2ProductRecords_.find(timeInSeconds);
      if (it != level2ProductRecords_.cend())
      {
         logger_->debug(
            "Level 2 product previously loaded, loading from cache");

         storedRecord = it->second;
      }
      else
      {
         level2ProductRecords_[timeInSeconds] = record;
      }
   }
   else if (record->radar_product_group() == common::RadarProductGroup::Level3)
   {
      std::unique_lock lock {level3ProductRecordMutex_};

      auto& productMap = level3ProductRecordsMap_[record->radar_product()];

      auto it = productMap.find(timeInSeconds);
      if (it != productMap.cend())
      {
         logger_->debug(
            "Level 3 product previously loaded, loading from cache");

         storedRecord = it->second;
      }
      else
      {
         productMap[timeInSeconds] = record;
      }
   }

   return storedRecord;
}

std::tuple<std::shared_ptr<wsr88d::rda::ElevationScan>,
           float,
           std::vector<float>>
RadarProductManager::GetLevel2Data(wsr88d::rda::DataBlockType dataBlockType,
                                   float                      elevation,
                                   std::chrono::system_clock::time_point time)
{
   std::shared_ptr<wsr88d::rda::ElevationScan> radarData    = nullptr;
   float                                       elevationCut = 0.0f;
   std::vector<float>                          elevationCuts;

   std::shared_ptr<types::RadarProductRecord> record =
      p->GetLevel2ProductRecord(time);

   if (record != nullptr)
   {
      std::tie(radarData, elevationCut, elevationCuts) =
         record->level2_file()->GetElevationScan(
            dataBlockType, elevation, time);
   }

   return std::tie(radarData, elevationCut, elevationCuts);
}

std::shared_ptr<wsr88d::rpg::Level3Message>
RadarProductManager::GetLevel3Data(const std::string& product,
                                   std::chrono::system_clock::time_point time)
{
   std::shared_ptr<wsr88d::rpg::Level3Message> message = nullptr;

   std::shared_ptr<types::RadarProductRecord> record =
      p->GetLevel3ProductRecord(product, time);

   if (record != nullptr)
   {
      message = record->level3_file()->message();
   }

   return message;
}

common::Level3ProductCategoryMap
RadarProductManager::GetAvailableLevel3Categories()
{
   std::shared_lock lock {p->availableCategoryMutex_};

   return p->availableCategoryMap_;
}

std::vector<std::string> RadarProductManager::GetLevel3Products()
{
   auto level3ProviderManager =
      p->GetLevel3ProviderManager(kDefaultLevel3Product_);
   return level3ProviderManager->provider_->GetAvailableProducts();
}

void RadarProductManager::UpdateAvailableProducts()
{
   std::lock_guard<std::mutex> guard(p->level3ProductsInitializeMutex_);

   if (p->level3ProductsInitialized_)
   {
      return;
   }

   // Although not complete here, only initialize once. Signal will be emitted
   // once complete.
   p->level3ProductsInitialized_ = true;

   logger_->debug("UpdateAvailableProducts()");

   util::async(
      [=]()
      {
         auto level3ProviderManager =
            p->GetLevel3ProviderManager(kDefaultLevel3Product_);
         level3ProviderManager->provider_->RequestAvailableProducts();
         auto updatedAwipsIdList =
            level3ProviderManager->provider_->GetAvailableProducts();

         std::unique_lock lock {p->availableCategoryMutex_};

         for (common::Level3ProductCategory category :
              common::Level3ProductCategoryIterator())
         {
            const auto& products =
               common::GetLevel3ProductsByCategory(category);

            std::unordered_map<std::string, std::vector<std::string>>
               availableProducts;

            for (const auto& product : products)
            {
               const auto& awipsIds =
                  common::GetLevel3AwipsIdsByProduct(product);

               std::vector<std::string> availableAwipsIds;

               for (const auto& awipsId : awipsIds)
               {
                  if (std::find(updatedAwipsIdList.cbegin(),
                                updatedAwipsIdList.cend(),
                                awipsId) != updatedAwipsIdList.cend())
                  {
                     availableAwipsIds.push_back(awipsId);
                  }
               }

               if (!availableAwipsIds.empty())
               {
                  availableProducts.insert_or_assign(
                     product, std::move(availableAwipsIds));
               }
            }

            if (!availableProducts.empty())
            {
               p->availableCategoryMap_.insert_or_assign(
                  category, std::move(availableProducts));
            }
            else
            {
               p->availableCategoryMap_.erase(category);
            }
         }

         emit Level3ProductsChanged();
      });
}

std::shared_ptr<RadarProductManager>
RadarProductManager::Instance(const std::string& radarSite)
{
   std::shared_ptr<RadarProductManager> instance        = nullptr;
   bool                                 instanceCreated = false;

   {
      std::lock_guard<std::mutex> guard(instanceMutex_);

      // Look up instance weak pointer
      auto it = instanceMap_.find(radarSite);
      if (it != instanceMap_.end())
      {
         // Attempt to convert the weak pointer to a shared pointer. It may have
         // been garbage collected.
         instance = it->second.lock();
      }

      // If no active instance was found, create a new one
      if (instance == nullptr)
      {
         instance = std::make_shared<RadarProductManager>(radarSite);
         instanceMap_.insert_or_assign(radarSite, instance);
         instanceCreated = true;
      }
   }

   if (instanceCreated)
   {
      emit RadarProductManagerNotifier::Instance().RadarProductManagerCreated(
         radarSite);
   }

   return instance;
}

#include "radar_product_manager.moc"

} // namespace manager
} // namespace qt
} // namespace scwx
