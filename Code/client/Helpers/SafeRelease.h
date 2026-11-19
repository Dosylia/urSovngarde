#pragma once

/**
 * Release a Windows graphics (COM) object, such as a Direct3D texture, and forget it.
 *
 * @param apObject The object, or null; null afterwards.
 */
template <class T> void SafeRelease(T*& apObject) noexcept
{
    if (apObject)
    {
        apObject->Release();
        apObject = nullptr;
    }
}
