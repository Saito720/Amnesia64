#pragma once

#include <graphics/Image.h>
#include <vector>

#include <span>

namespace hpl {
    struct HPLTexture;
    class cTextureManager;

    class AnimatedImage : public iResourceBase {
    public:
        AnimatedImage();
        AnimatedImage(const tString& asName, const tWString& asFullPath);
        void SetImages(std::span<std::shared_ptr<HPLTexture>>);
        virtual ~AnimatedImage();

        Image* GetImage() const;

        virtual bool Reload() override;
        virtual void Unload() override;
        virtual void Destroy() override;

        void Update(float afTimeStep);
        void SetFrameTime(float frameTime);
        float GetFrameTime();
        eTextureAnimMode GetAnimMode();
        void SetAnimMode(eTextureAnimMode aMode);

    private:
        std::vector<std::shared_ptr<HPLTexture>> m_images;

        float m_frameTime = 0.0f;
        float m_timeCount = 0.0f;
        float m_timeDir = 1.0f;
        eTextureAnimMode m_animMode = eTextureAnimMode_Loop;
        cTextureManager* m_textureManager = nullptr;
    };
} // namespace hpl

