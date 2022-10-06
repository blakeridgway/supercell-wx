#include <scwx/qt/util/texture_atlas.hpp>
#include <scwx/qt/util/streams.hpp>
#include <scwx/util/logger.hpp>

#include <shared_mutex>
#include <unordered_map>

#pragma warning(push, 0)
#pragma warning(disable : 4714)
#include <boost/gil/extension/io/png.hpp>
#include <boost/iostreams/stream.hpp>
#include <stb_rect_pack.h>
#include <QFile>
#pragma warning(pop)

namespace scwx
{
namespace qt
{
namespace util
{

static const std::string logPrefix_ = "scwx::qt::util::texture_atlas";
static const auto        logger_    = scwx::util::Logger::Create(logPrefix_);

struct TextureInfo
{
   TextureInfo(boost::gil::point_t position, boost::gil::point_t size) :
       position_ {position}, size_ {size}
   {
   }

   boost::gil::point_t position_;
   boost::gil::point_t size_;
};

class TextureAtlas::Impl
{
public:
   explicit Impl() :
       texturePathMap_ {},
       texturePathMutex_ {},
       atlas_ {},
       atlasMap_ {},
       atlasMutex_ {}
   {
   }
   ~Impl() {}

   static boost::gil::rgba8_image_t LoadImage(const std::string& imagePath);

   std::unordered_map<std::string, std::string> texturePathMap_;
   std::shared_mutex                            texturePathMutex_;

   boost::gil::rgba8_image_t                    atlas_;
   std::unordered_map<std::string, TextureInfo> atlasMap_;
   std::shared_mutex                            atlasMutex_;
};

TextureAtlas::TextureAtlas() : p(std::make_unique<Impl>()) {}
TextureAtlas::~TextureAtlas() = default;

TextureAtlas::TextureAtlas(TextureAtlas&&) noexcept            = default;
TextureAtlas& TextureAtlas::operator=(TextureAtlas&&) noexcept = default;

void TextureAtlas::RegisterTexture(const std::string& name,
                                   const std::string& path)
{
   std::unique_lock lock(p->texturePathMutex_);
   p->texturePathMap_.insert_or_assign(name, path);
}

void TextureAtlas::BuildAtlas(size_t width, size_t height)
{
   logger_->debug("Building {}x{} texture atlas", width, height);

   if (width > INT_MAX || height > INT_MAX)
   {
      logger_->error("Cannot build texture atlas of size {}x{}", width, height);
      return;
   }

   std::vector<std::pair<std::string, boost::gil::rgba8_image_t>> images;
   std::vector<stbrp_rect>                                        stbrpRects;

   // Load images
   {
      // Take a read lock on the texture path map
      std::shared_lock lock(p->texturePathMutex_);

      // For each registered texture
      std::for_each(p->texturePathMap_.cbegin(),
                    p->texturePathMap_.cend(),
                    [&](const auto& pair)
                    {
                       // Load texture image
                       boost::gil::rgba8_image_t image =
                          Impl::LoadImage(pair.second);

                       if (image.width() > 0u && image.height() > 0u)
                       {
                          // Store STB rectangle pack data in a vector
                          stbrpRects.push_back(stbrp_rect {
                             0,
                             static_cast<stbrp_coord>(image.width()),
                             static_cast<stbrp_coord>(image.height()),
                             0,
                             0,
                             0});

                          // Store image data in a vector
                          images.emplace_back(pair.first, std::move(image));
                       }
                    });
   }

   // Pack images
   {
      logger_->trace("Packing {} images", images.size());

      // Optimal number of nodes = width
      stbrp_context           stbrpContext;
      std::vector<stbrp_node> stbrpNodes(width);

      stbrp_init_target(&stbrpContext,
                        static_cast<int>(width),
                        static_cast<int>(height),
                        stbrpNodes.data(),
                        static_cast<int>(stbrpNodes.size()));

      // Pack loaded textures
      stbrp_pack_rects(
         &stbrpContext, stbrpRects.data(), static_cast<int>(stbrpRects.size()));
   }

   // Lock atlas
   std::unique_lock lock(p->atlasMutex_);

   // Clear index
   p->atlasMap_.clear();

   // Clear atlas
   p->atlas_.recreate(width, height);
   boost::gil::rgba8_view_t atlasView = boost::gil::view(p->atlas_);

   // Populate atlas
   logger_->trace("Populating atlas");

   for (size_t i = 0; i < images.size(); i++)
   {
      // If the image was packed successfully
      if (stbrpRects[i].was_packed != 0)
      {
         // Populate the atlas
         boost::gil::rgba8c_view_t imageView =
            boost::gil::const_view(images[i].second);
         boost::gil::rgba8_view_t atlasSubView =
            boost::gil::subimage_view(atlasView,
                                      stbrpRects[i].x,
                                      stbrpRects[i].y,
                                      imageView.width(),
                                      imageView.height());

         boost::gil::copy_pixels(imageView, atlasSubView);

         // Add texture image to the index
         p->atlasMap_.emplace(
            std::piecewise_construct,
            std::forward_as_tuple(images[i].first),
            std::forward_as_tuple(
               boost::gil::point_t {stbrpRects[i].x, stbrpRects[i].y},
               boost::gil::point_t {imageView.width(), imageView.height()}));
      }
      else
      {
         logger_->warn("Unable to pack texture: {}", images[i].first);
      }
   }
}

GLuint TextureAtlas::BufferAtlas(gl::OpenGLFunctions& gl)
{
   GLuint texture = GL_INVALID_INDEX;

   std::shared_lock lock(p->atlasMutex_);

   if (p->atlas_.width() > 0u && p->atlas_.height() > 0u)
   {
      boost::gil::rgba8_view_t               view = boost::gil::view(p->atlas_);
      std::vector<boost::gil::rgba8_pixel_t> pixelData(view.width() *
                                                       view.height());

      boost::gil::copy_pixels(
         view,
         boost::gil::interleaved_view(view.width(),
                                      view.height(),
                                      pixelData.data(),
                                      view.width() *
                                         sizeof(boost::gil::rgba8_pixel_t)));

      lock.unlock();

      gl.glGenTextures(1, &texture);
      gl.glBindTexture(GL_TEXTURE_2D, texture);

      gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      gl.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

      gl.glTexImage2D(GL_TEXTURE_2D,
                      0,
                      GL_RGBA,
                      view.width(),
                      view.height(),
                      0,
                      GL_RGBA,
                      GL_UNSIGNED_BYTE,
                      pixelData.data());
   }

   return texture;
}

boost::gil::rgba8_image_t
TextureAtlas::Impl::LoadImage(const std::string& imagePath)
{
   logger_->debug("Loading image: {}", imagePath);

   boost::gil::rgba8_image_t image;

   QFile imageFile(imagePath.c_str());

   imageFile.open(QIODevice::ReadOnly);

   if (!imageFile.isOpen())
   {
      logger_->error("Could not open image: {}", imagePath);
      return std::move(image);
   }

   boost::iostreams::stream<util::IoDeviceSource> dataStream(imageFile);

   boost::gil::image<boost::gil::rgba8_pixel_t, false> x;

   try
   {
      boost::gil::read_and_convert_image(
         dataStream, image, boost::gil::png_tag());
   }
   catch (const std::exception& ex)
   {
      logger_->error("Error reading image: {}", ex.what());
      return std::move(image);
   }

   return std::move(image);
}

TextureAtlas& TextureAtlas::Instance()
{
   static TextureAtlas instance_ {};
   return instance_;
}

} // namespace util
} // namespace qt
} // namespace scwx
