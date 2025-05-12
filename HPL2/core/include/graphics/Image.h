/**
 * Copyright 2023 Michael Pollind
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include "graphics/Bitmap.h"
#include "graphics/RIBoostrap.h"
#include "math/MathTypes.h"
#include "resources/ResourceBase.h"
#include "system/SystemTypes.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace hpl {

    class cBitmap;
    class Image : public iResourceBase {
    public:

        Image();
        Image(const tString& asName, const tWString& asFullPath);

        ~Image();
        Image(Image&& other);
        Image(const Image& other) = delete;

        Image& operator=(const Image& other) = delete;
        void operator=(Image&& other);

        virtual bool Reload() override;
        virtual void Unload() override;
        virtual void Destroy() override;

        //inline uint16_t GetWidth() const {
        //    ASSERT(m_texture.IsValid());
        //    return m_texture.m_handle->mWidth;
        //}
        //inline uint16_t GetHeight() const {
        //    ASSERT(m_texture.IsValid());
        //    return m_texture.m_handle->mHeight;
        //}

        //cVector2l GetImageSize() const {
        //    if (m_texture.IsValid()) {
        //        return cVector2l(m_texture.m_handle->mWidth, m_texture.m_handle->mHeight);
        //    }
        //    return cVector2l(0, 0);
        //}

        std::shared_ptr<HPLTexture> textures;
    };

} // namespace hpl

