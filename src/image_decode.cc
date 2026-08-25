#include "ftxui/ext/image_decode.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>

#define STB_IMAGE_IMPLEMENTATION
#include "ftxui/ext/stb_image.h"

namespace ftxui::ext
{

    namespace
    {

        // stb_image wants a contiguous buffer; read the file once.
        bool ReadFileBytes(const std::filesystem::path &path, std::vector<char> &out)
        {
            std::ifstream in(path, std::ios::binary);
            if (!in)
                return false;
            in.seekg(0, std::ios::end);
            const std::streamsize size = in.tellg();
            if (size < 0)
                return false;
            in.seekg(0, std::ios::beg);
            out.resize(static_cast<std::size_t>(size));
            if (size > 0 && !in.read(out.data(), size))
                return false;
            return true;
        }

        bool ToImage(int width, int height, const stbi_uc *rgba, std::size_t pixel_count,
                     Image &out)
        {
            if (width <= 0 || height <= 0 || rgba == nullptr)
                return false;
            out.width = width;
            out.height = height;
            out.pixels.assign(rgba, rgba + pixel_count * 4);
            return true;
        }

    } // namespace

    bool LoadImage(const std::filesystem::path &path, Image &out)
    {
        std::vector<char> bytes;
        if (!ReadFileBytes(path, bytes))
            return false;

        int width = 0, height = 0, channels = 0;
        stbi_uc *data = stbi_load_from_memory(
            reinterpret_cast<const stbi_uc *>(bytes.data()),
            static_cast<int>(bytes.size()), &width, &height, &channels, 4);
        if (data == nullptr)
            return false;
        const bool ok = ToImage(width, height, data,
                                static_cast<std::size_t>(width) * height, out);
        stbi_image_free(data);
        return ok;
    }

    bool LoadGifFrames(const std::filesystem::path &path,
                       std::vector<Image> &frames,
                       std::vector<int> &delays_ms)
    {
        std::vector<char> bytes;
        if (!ReadFileBytes(path, bytes))
            return false;

        int width = 0, height = 0, frame_count = 0, channels = 0;
        int *delays = nullptr;
        stbi_uc *data = stbi_load_gif_from_memory(
            reinterpret_cast<const stbi_uc *>(bytes.data()),
            static_cast<int>(bytes.size()), &delays, &width, &height, &frame_count,
            &channels, 4);
        if (data == nullptr)
            return false;

        frames.clear();
        delays_ms.clear();
        const std::size_t frame_size =
            static_cast<std::size_t>(width) * height * 4;
        for (int i = 0; i < frame_count; ++i)
        {
            Image frame;
            const bool ok = ToImage(width, height, data + i * frame_size, frame_size / 4,
                                    frame);
            if (!ok)
            {
                stbi_image_free(data);
                stbi_image_free(delays);
                return false;
            }
            frames.push_back(std::move(frame));
            delays_ms.push_back(delays != nullptr ? std::max(1, delays[i]) : 100);
        }
        stbi_image_free(data);
        stbi_image_free(delays);
        return !frames.empty();
    }

} // namespace ftxui::ext
