#include "graphics/Image.h"

namespace hpl {

Image::Image() : iResourceBase("", _W(""), 0) {
  // m_textures = std::make_shared(Texture{0})
}

Image::Image(const tString &asName, const tWString &asFullPath)
    : iResourceBase(asName, asFullPath, 0) {}

bool Image::Reload() { return false; }

void Image::Unload() {}

void Image::Destroy() {}

Image::Image(Image &&other)
    : iResourceBase(other.GetName(), other.GetFullPath(), 0) {
  image = std::move(other.image);
}

Image::~Image() {}

void Image::operator=(Image &&other) { image = std::move(other.image); }
} // namespace hpl
