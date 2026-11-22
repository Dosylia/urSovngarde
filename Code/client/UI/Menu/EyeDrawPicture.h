#pragma once

#ifdef SKYRIMVR

#include <d3d11.h>

#include <cstdint>
#include <vector>

/**
 * One picture handed from the update thread to the render thread, and its texture on the game's device: the panel's page, or a banner layer's picture.
 * Give and TakeOver run under the eye drawer's lock, which the two threads share; everything else runs on the render thread alone.
 */
class EyeDrawPicture
{
public:
    /**
     * What putting the picture on the game's device came to.
     */
    enum class Upload : uint8_t
    {
        kReady,     // the texture holds the picture last taken over
        kNoPicture, // no picture was given yet
        kFailed,    // the texture could not be made
    };

    /**
     * Hand a new picture to the render thread, by swapping: the caller gets an older buffer back to fill next time.
     * Under the eye drawer's lock.
     *
     * @param aPixels The whole picture, BGRA premultiplied, top row first; ignored when its size does not match the width and height.
     * @param aWidth Its width in pixels.
     * @param aHeight Its height in pixels.
     */
    void Give(std::vector<uint8_t>& aPixels, uint32_t aWidth, uint32_t aHeight) noexcept;

    /**
     * Take over the newest picture given, if any, for the render thread to put on the device.
     * Under the eye drawer's lock, on the render thread, together with the rest of the frame's scene.
     */
    void TakeOver() noexcept;

    /**
     * Put the picture taken over on the game's device, with its smaller levels: a picture lands on far fewer eye pixels than it has, and thin text would shimmer.
     * The render thread only, without the lock, so an upload never holds up the update thread.
     *
     * @param apDevice The game's device.
     * @param apImmediate The game's device context, which runs the upload.
     * @return Whether the texture holds the picture.
     */
    Upload PutOnDevice(ID3D11Device* apDevice, ID3D11DeviceContext* apImmediate) noexcept;

    /**
     * Get the texture's view, for the shaders.
     *
     * @return The view, or null before the first upload.
     */
    ID3D11ShaderResourceView* GetView() const noexcept;

    /**
     * Get the picture taken over, as it was given. Render thread only.
     *
     * @return Its pixels, BGRA premultiplied, top row first; empty before the first picture.
     */
    const std::vector<uint8_t>& GetPixels() const noexcept;

    /**
     * Get the width of the picture taken over. Render thread only.
     *
     * @return Its width in pixels; 0 before the first picture.
     */
    uint32_t GetWidth() const noexcept;

    /**
     * Get the height of the picture taken over. Render thread only.
     *
     * @return Its height in pixels; 0 before the first picture.
     */
    uint32_t GetHeight() const noexcept;

private:
    /**
     * Make the texture again, at the size of the picture taken over.
     *
     * @param apDevice The game's device.
     * @return True when it was made.
     */
    bool MakeTexture(ID3D11Device* apDevice) noexcept;

    // Under the lock: the newest picture given, its size, and whether the render thread has yet to take it.
    std::vector<uint8_t> m_givenPixels;
    uint32_t m_givenWidth = 0;
    uint32_t m_givenHeight = 0;
    bool m_isGiven = false;
    // Render thread only: the picture taken over, its size, and whether the texture has yet to get it.
    std::vector<uint8_t> m_pixels;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    bool m_isUploadDue = false;
    // Render thread only: the texture, its view, and the size it was made at.
    ID3D11Texture2D* m_pTexture = nullptr;
    ID3D11ShaderResourceView* m_pView = nullptr;
    uint32_t m_textureWidth = 0;
    uint32_t m_textureHeight = 0;
};

#endif
