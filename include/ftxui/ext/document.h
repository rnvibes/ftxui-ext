// ftxui-ext/include/ftxui/ext/document.h - the shared document spine.
//
// Both the markdown and org documents derive from this: an owned source
// view plus the arena that backs every text view the blocks reference.
// Text that is already part of the source stays a view (zero-copy); the
// few synthesized strings (markers, prefixes, computed values) are
// interned into the arena and live exactly as long as the document.
// Documents are cheap to move or copy: the arena is shared state.
#pragma once

#include <cstddef>
#include <cstring>
#include <memory>
#include <memory_resource>
#include <string_view>
#include <utility>

namespace ftxui::ext
{
    class Document
    {
    public:
        Document() = default;
        explicit Document(std::string_view source) : source_(source) {}
        Document(const Document&) = default;
        Document& operator=(const Document&) = default;
        Document(Document&&) noexcept = default;
        Document& operator=(Document&&) noexcept = default;

        std::string_view source() const { return source_; }
        void set_source(std::string_view s) { source_ = s; }

        // The arena backing all block text; derived documents allocate
        // their blocks and vectors from it.
        std::shared_ptr<std::pmr::monotonic_buffer_resource> arena() const
        {
            return arena_;
        }

        // Swaps source and arena; derived documents swap their blocks too
        // so every arena stays with the data allocated from it.
        void swap(Document& other) noexcept
        {
            std::swap(source_, other.source_);
            std::swap(arena_, other.arena_);
        }

        // Copies s into the arena. The returned view stays valid as long
        // as this document (or any copy sharing the arena) lives.
        std::string_view intern(std::string_view s)
        {
            if (s.empty())
                return {};
            char* p = static_cast<char*>(arena_->allocate(s.size(), 1));
            std::memcpy(p, s.data(), s.size());
            return {p, s.size()};
        }

    private:
        std::string_view source_;
        std::shared_ptr<std::pmr::monotonic_buffer_resource> arena_ =
            std::make_shared<std::pmr::monotonic_buffer_resource>();
    };
} // namespace ftxui::ext
