#include <TiltedOnlinePCH.h>

#ifdef SKYRIMVR

#include <UI/Menu/EyeDrawPicture.h>

#include <Helpers/SafeRelease.h>

namespace
{
// Every picture has four bytes a pixel: blue, green, red, alpha.
constexpr uint32_t kBytesPerPixel = 4;
} // namespace

/**
 * Hand a new picture to the render thread, by swapping: the caller gets an older buffer back to fill next time.
 * Under the eye drawer's lock.
 *
 * @param aPixels The whole picture, BGRA premultiplied, top row first; ignored when its size does not match the width and height.
 * @param aWidth Its width in pixels.
 * @param aHeight Its height in pixels.
 */
void EyeDrawPicture::Give(std::vector<uint8_t>& aPixels, const uint32_t aWidth, const uint32_t aHeight) noexcept
{
    if (aPixels.size() != static_cast<size_t>(aWidth) * aHeight * kBytesPerPixel)
        return;
    m_givenPixels.swap(aPixels);
    m_givenWidth = aWidth;
    m_givenHeight = aHeight;
    m_isGiven = true;
}

/**
 * Take over the newest picture given, if any, for the render thread to put on the device.
 * Under the eye drawer's lock, on the render thread, together with the rest of the frame's scene.
 */
void EyeDrawPicture::TakeOver() noexcept
{
    if (!m_isGiven)
        return;
    // Swapped, so the buffer it held goes back to the update thread with the next Give.
    m_pixels.swap(m_givenPixels);
    m_width = m_givenWidth;
    m_height = m_givenHeight;
    m_isGiven = false;
    m_isUploadDue = true;
}

/**
 * Put the picture taken over on the game's device, with its smaller levels: a picture lands on far fewer eye pixels than it has, and thin text would shimmer.
 * The render thread only, without the lock, so an upload never holds up the update thread.
 *
 * @param apDevice The game's device.
 * @param apImmediate The game's device context, which runs the upload.
 * @return Whether the texture holds the picture.
 */
EyeDrawPicture::Upload EyeDrawPicture::PutOnDevice(ID3D11Device* apDevice, ID3D11DeviceContext* apImmediate) noexcept
{
    if (m_pixels.empty())
        return Upload::kNoPicture;
    if (!m_pTexture || m_textureWidth != m_width || m_textureHeight != m_height)
    {
        if (!MakeTexture(apDevice))
            return Upload::kFailed;
        m_isUploadDue = true;
    }
    if (m_isUploadDue)
    {
        apImmediate->UpdateSubresource(m_pTexture, 0, nullptr, m_pixels.data(), m_width * kBytesPerPixel, 0);
        apImmediate->GenerateMips(m_pView);
        m_isUploadDue = false;
    }
    return Upload::kReady;
}

/**
 * Get the texture's view, for the shaders.
 *
 * @return The view, or null before the first upload.
 */
ID3D11ShaderResourceView* EyeDrawPicture::GetView() const noexcept
{
    return m_pView;
}

/**
 * Get the picture taken over, as it was given. Render thread only.
 *
 * @return Its pixels, BGRA premultiplied, top row first; empty before the first picture.
 */
const std::vector<uint8_t>& EyeDrawPicture::GetPixels() const noexcept
{
    return m_pixels;
}

/**
 * Get the width of the picture taken over. Render thread only.
 *
 * @return Its width in pixels; 0 before the first picture.
 */
uint32_t EyeDrawPicture::GetWidth() const noexcept
{
    return m_width;
}

/**
 * Get the height of the picture taken over. Render thread only.
 *
 * @return Its height in pixels; 0 before the first picture.
 */
uint32_t EyeDrawPicture::GetHeight() const noexcept
{
    return m_height;
}

/**
 * Make the texture again, at the size of the picture taken over.
 *
 * @param apDevice The game's device.
 * @return True when it was made.
 */
bool EyeDrawPicture::MakeTexture(ID3D11Device* apDevice) noexcept
{
    SafeRelease(m_pView);
    SafeRelease(m_pTexture);
    m_textureWidth = 0;
    m_textureHeight = 0;
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = m_width;
    desc.Height = m_height;
    desc.MipLevels = 0;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
    if (FAILED(apDevice->CreateTexture2D(&desc, nullptr, &m_pTexture)) || FAILED(apDevice->CreateShaderResourceView(m_pTexture, nullptr, &m_pView)))
        return false;
    m_textureWidth = m_width;
    m_textureHeight = m_height;
    return true;
}

#endif
