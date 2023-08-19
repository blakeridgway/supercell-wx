#include <scwx/qt/map/placefile_layer.hpp>
#include <scwx/qt/gl/draw/placefile_icons.hpp>
#include <scwx/qt/gl/draw/placefile_polygons.hpp>
#include <scwx/qt/gl/draw/placefile_text.hpp>
#include <scwx/qt/manager/placefile_manager.hpp>
#include <scwx/util/logger.hpp>

#include <boost/asio/post.hpp>
#include <boost/asio/thread_pool.hpp>

namespace scwx
{
namespace qt
{
namespace map
{

static const std::string logPrefix_ = "scwx::qt::map::placefile_layer";
static const auto        logger_    = scwx::util::Logger::Create(logPrefix_);

class PlacefileLayer::Impl
{
public:
   explicit Impl(PlacefileLayer*             self,
                 std::shared_ptr<MapContext> context,
                 const std::string&          placefileName) :
       self_ {self},
       placefileName_ {placefileName},
       placefileIcons_ {std::make_shared<gl::draw::PlacefileIcons>(context)},
       placefilePolygons_ {
          std::make_shared<gl::draw::PlacefilePolygons>(context)},
       placefileText_ {
          std::make_shared<gl::draw::PlacefileText>(context, placefileName)}
   {
      ConnectSignals();
   }
   ~Impl() = default;

   void ConnectSignals();

   boost::asio::thread_pool threadPool_ {1};

   PlacefileLayer* self_;

   std::string placefileName_;
   std::mutex  dataMutex_ {};
   bool        dirty_ {true};

   std::shared_ptr<gl::draw::PlacefileIcons>    placefileIcons_;
   std::shared_ptr<gl::draw::PlacefilePolygons> placefilePolygons_;
   std::shared_ptr<gl::draw::PlacefileText>     placefileText_;
};

PlacefileLayer::PlacefileLayer(std::shared_ptr<MapContext> context,
                               const std::string&          placefileName) :
    DrawLayer(context),
    p(std::make_unique<PlacefileLayer::Impl>(this, context, placefileName))
{
   AddDrawItem(p->placefileIcons_);
   AddDrawItem(p->placefilePolygons_);
   AddDrawItem(p->placefileText_);
}

PlacefileLayer::~PlacefileLayer() = default;

void PlacefileLayer::Impl::ConnectSignals()
{
   auto placefileManager = manager::PlacefileManager::Instance();

   QObject::connect(placefileManager.get(),
                    &manager::PlacefileManager::PlacefileUpdated,
                    self_,
                    [this](const std::string& name)
                    {
                       if (name == placefileName_)
                       {
                          self_->ReloadData();
                       }
                    });
}

std::string PlacefileLayer::placefile_name() const
{
   return p->placefileName_;
}

void PlacefileLayer::set_placefile_name(const std::string& placefileName)
{
   p->placefileName_ = placefileName;
   p->dirty_         = true;

   p->placefileText_->set_placefile_name(placefileName);
}

void PlacefileLayer::Initialize()
{
   logger_->debug("Initialize()");

   DrawLayer::Initialize();
}

void PlacefileLayer::Render(
   const QMapLibreGL::CustomLayerRenderParameters& params)
{
   gl::OpenGLFunctions& gl = context()->gl();

   // Set OpenGL blend mode for transparency
   gl.glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

   std::shared_ptr<manager::PlacefileManager> placefileManager =
      manager::PlacefileManager::Instance();

   auto placefile = placefileManager->placefile(p->placefileName_);

   // Render placefile
   if (placefile != nullptr)
   {
      bool thresholded =
         placefileManager->placefile_thresholded(placefile->name());
      p->placefileIcons_->set_thresholded(thresholded);
      p->placefilePolygons_->set_thresholded(thresholded);
      p->placefileText_->set_thresholded(thresholded);

      if (p->dirty_)
      {
         // Reset Placefile Icons
         p->placefileIcons_->Reset();
         p->placefileIcons_->SetIconFiles(placefile->icon_files(),
                                          placefile->name());

         // Reset Placefile Text
         p->placefileText_->Reset();

         for (auto& drawItem : placefile->GetDrawItems())
         {
            switch (drawItem->itemType_)
            {
            case gr::Placefile::ItemType::Text:
               p->placefileText_->AddText(
                  std::static_pointer_cast<gr::Placefile::TextDrawItem>(
                     drawItem));
               break;

            case gr::Placefile::ItemType::Icon:
               p->placefileIcons_->AddIcon(
                  std::static_pointer_cast<gr::Placefile::IconDrawItem>(
                     drawItem));
               break;

            default:
               break;
            }
         }
      }
   }

   DrawLayer::Render(params);

   SCWX_GL_CHECK_ERROR();
}

void PlacefileLayer::Deinitialize()
{
   logger_->debug("Deinitialize()");

   DrawLayer::Deinitialize();
}

void PlacefileLayer::ReloadData()
{
   // TODO: No longer needed after moving Icon and Text Render items here
   p->dirty_ = true;

   boost::asio::post(
      p->threadPool_,
      [this]()
      {
         logger_->debug("ReloadData: {}", p->placefileName_);

         std::unique_lock lock {p->dataMutex_};

         std::shared_ptr<manager::PlacefileManager> placefileManager =
            manager::PlacefileManager::Instance();

         auto placefile = placefileManager->placefile(p->placefileName_);
         if (placefile == nullptr)
         {
            return;
         }

         // Reset Placefile Polygons
         p->placefilePolygons_->StartPolygons();

         for (auto& drawItem : placefile->GetDrawItems())
         {
            switch (drawItem->itemType_)
            {
            case gr::Placefile::ItemType::Text:
               // TODO
               break;

            case gr::Placefile::ItemType::Icon:
               // TODO
               break;

            case gr::Placefile::ItemType::Polygon:
               p->placefilePolygons_->AddPolygon(
                  std::static_pointer_cast<gr::Placefile::PolygonDrawItem>(
                     drawItem));
               break;

            default:
               break;
            }
         }

         // Finish Placefile Polygons
         p->placefilePolygons_->FinishPolygons();
      });
}

} // namespace map
} // namespace qt
} // namespace scwx
