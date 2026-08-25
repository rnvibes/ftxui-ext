#include "ftxui/ext/file_source.h"

#include "ftxui/ext/utf8.h"

#include <algorithm>
#include <cctype>
#include <fstream>

namespace ftxui::ext
{

    namespace
    {

        bool IsDotName(const std::string &name)
        {
            return !name.empty() && name.front() == '.';
        }

        // Trees that are enormous, machine-generated, and never what someone
        // meant to ask questions about. The dotted names are listed even though
        // IsDotName already excludes them under the default options: pressing
        // '.' to reveal hidden files should not also mean walking .git.
        bool IsHeavyDir(const std::string &name)
        {
            static const std::string kHeavy[] = {
                "node_modules", "build", "dist", "target", "__pycache__",
                "venv", "Pods", "DerivedData", ".git", ".venv", ".cache",
                ".next", ".direnv", ".tox"};
            return std::find(std::begin(kHeavy), std::end(kHeavy), name) !=
                   std::end(kHeavy);
        }

        int CompareNamesCI(const std::string &a, const std::string &b)
        {
            const std::size_t n = std::min(a.size(), b.size());
            for (std::size_t i = 0; i < n; ++i)
            {
                const int ca = std::tolower(static_cast<unsigned char>(a[i]));
                const int cb = std::tolower(static_cast<unsigned char>(b[i]));
                if (ca != cb)
                    return ca < cb ? -1 : 1;
            }
            if (a.size() == b.size())
                return 0;
            return a.size() < b.size() ? -1 : 1;
        }

    } // namespace

    DirectoryListing read_directory(const std::filesystem::path &dir, bool show_hidden)
    {
        DirectoryListing listing;
        listing.path = dir;

        std::error_code ec;
        std::filesystem::directory_iterator it(dir, ec);
        if (ec)
        {
            listing.error = ec.message();
            return listing;
        }
        const std::filesystem::directory_iterator end;
        for (; it != end; it.increment(ec))
        {
            if (ec)
                break;
            const std::filesystem::directory_entry &entry = *it;
            const std::string name = entry.path().filename().string();
            if (!show_hidden && IsDotName(name))
                continue;
            std::error_code entry_ec;
            const bool is_dir = entry.is_directory(entry_ec);
            if (entry_ec)
                continue;
            FileEntry item;
            item.name = name;
            item.is_dir = is_dir;
            if (!is_dir)
            {
                const uintmax_t size = entry.file_size(entry_ec);
                item.size = entry_ec ? 0 : size;
            }
            listing.entries.push_back(std::move(item));
        }

        std::stable_sort(listing.entries.begin(), listing.entries.end(),
                         [](const FileEntry &a, const FileEntry &b)
                         {
                             if (a.is_dir != b.is_dir)
                                 return a.is_dir;
                             const int cmp = CompareNamesCI(a.name, b.name);
                             if (cmp != 0)
                                 return cmp < 0;
                             return a.name < b.name;
                         });
        return listing;
    }

    DirectoryWalk collect_files_recursive(const std::filesystem::path &root,
                                          const WalkOptions &opts)
    {
        namespace fs = std::filesystem;
        DirectoryWalk walk;

        std::error_code ec;
        // skip_permission_denied absorbs the common EACCES mid-walk.
        // follow_directory_symlink is deliberately NOT set: not following is
        // what makes a symlink loop impossible instead of merely bounded.
        fs::recursive_directory_iterator it(
            root, fs::directory_options::skip_permission_denied, ec);
        if (ec)
        {
            walk.error = ec.message();
            return walk;
        }

        const fs::recursive_directory_iterator end;
        std::size_t visits = 0;
        for (; it != end; it.increment(ec))
        {
            // A failed increment leaves the iterator in an unspecified state,
            // so this stops rather than clearing and continuing -- clearing can
            // spin forever on the same broken entry.
            if (ec)
                break;
            if (opts.cancelled && opts.cancelled())
            {
                walk.cancelled = true;
                break;
            }
            if (++visits > opts.max_visits)
            {
                walk.truncated = true;
                break;
            }

            const fs::directory_entry &entry = *it;
            const std::string name = entry.path().filename().string();

            std::error_code entry_ec;
            // Asked before is_directory, which resolves the link: a symlinked
            // directory would otherwise read as a directory this declines to
            // enter, and a symlinked file would still be collected from outside
            // the tree the caller pointed at.
            if (entry.is_symlink(entry_ec) || entry_ec)
            {
                it.disable_recursion_pending();
                continue;
            }

            const bool is_dir = entry.is_directory(entry_ec);
            if (entry_ec)
                continue;

            if (is_dir)
            {
                // disable_recursion_pending only means anything while this
                // directory is the current entry, which is why the prune
                // happens here rather than by skipping ahead.
                const bool prune = (!opts.show_hidden && IsDotName(name)) ||
                                   (opts.skip_heavy_dirs && IsHeavyDir(name)) ||
                                   it.depth() >= opts.max_depth;
                if (prune)
                {
                    it.disable_recursion_pending();
                    ++walk.pruned_dirs;
                }
                continue;
            }

            if (!opts.show_hidden && IsDotName(name))
                continue;
            if (!entry.is_regular_file(entry_ec) || entry_ec)
                continue;
            if (walk.files.size() >= opts.max_files)
            {
                walk.truncated = true;
                break;
            }
            walk.files.push_back(entry.path());
        }
        return walk;
    }

    FileKind classify_file(const std::filesystem::path &path, const FileEntry &entry)
    {
        if (entry.is_dir)
            return FileKind::Directory;
        return classify_file(path);
    }

    FileKind classify_file(const std::filesystem::path &path)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in)
            return FileKind::Text; // Unreadable: let the preview report it.
        char buffer[kBinarySniffBytes];
        in.read(buffer, sizeof(buffer));
        const std::streamsize got = in.gcount();
        for (std::streamsize i = 0; i < got; ++i)
        {
            if (buffer[i] == '\0')
                return FileKind::Binary;
        }
        return FileKind::Text;
    }

    Preview read_text_preview(const std::filesystem::path &path, std::size_t max_bytes)
    {
        Preview preview;

        std::ifstream in(path, std::ios::binary);
        if (!in)
        {
            preview.error = "cannot open file";
            return preview;
        }
        in.seekg(0, std::ios::end);
        const std::streamoff size = in.tellg();
        in.seekg(0, std::ios::beg);
        if (size < 0)
        {
            preview.error = "cannot determine file size";
            return preview;
        }

        const std::size_t want = static_cast<std::size_t>(size);
        preview.truncated = want > max_bytes;
        const std::size_t to_read = preview.truncated ? max_bytes : want;

        preview.text.resize(to_read);
        if (to_read > 0)
        {
            in.read(preview.text.data(), static_cast<std::streamsize>(to_read));
            if (!in && in.gcount() < static_cast<std::streamsize>(to_read))
                preview.text.resize(static_cast<std::size_t>(in.gcount()));
        }

        if (preview.truncated && !preview.text.empty())
        {
            // The first unread byte decides the cut: a continuation byte means
            // the glyph straddles the cap, so back off to that glyph's lead
            // byte. Any other first-unread byte (or EOF) means `to_read` is
            // already a glyph boundary and must not be nudged --
            // utf8_prev_boundary alone would drop a complete final glyph
            // (e.g. "café" capped at the full é).
            char next = '\0';
            const bool have_next = static_cast<bool>(in.get(next));
            if (have_next && utf8_is_continuation(next))
            {
                const int boundary = utf8_prev_boundary(
                    preview.text, static_cast<int>(preview.text.size()));
                preview.text.resize(static_cast<std::size_t>(boundary));
            }
        }
        return preview;
    }

} // namespace ftxui::ext
