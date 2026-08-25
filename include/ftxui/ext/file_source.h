#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace ftxui::ext
{

    // Reading things off disk, safely, for something else to display.
    //
    // Deliberately free of FTXUI and of any pane: these are answers about
    // files, not about widgets, and the DocumentRouter needs them without
    // needing a terminal. They lived in file_explorer.h until the router
    // started using read_text_preview and dragged the whole component library
    // in behind it -- decoding a PDF should not require linking a UI toolkit
    // to test.

    struct FileEntry
    {
        std::string name;
        bool is_dir = false;
        uintmax_t size = 0;
    };

    struct DirectoryListing
    {
        std::filesystem::path path;
        std::vector<FileEntry> entries;
        // Empty on success; a human-readable message when the directory could
        // not be read. Errors are captured, never thrown.
        std::string error;
    };

    // Reads `dir` and returns its entries, directories first then
    // case-insensitive by name. Dot-files (leading '.') are omitted unless
    // `show_hidden` is true. `.` and `..` are never listed. A missing or
    // unreadable directory yields an empty listing with `error` set.
    DirectoryListing read_directory(const std::filesystem::path &dir, bool show_hidden);

    // Bounds on a recursive walk. Every one of these is a refusal to let a
    // keystroke cost unbounded work: `collect_files_recursive` is reached by
    // pressing Space on a directory row, and that row might be `/`.
    struct WalkOptions
    {
        // Same rule read_directory applies, so a walk stages what the pane shows.
        bool show_hidden = false;

        // Files returned. Bounded by threads rather than by bytes: every staged
        // file owns a VectorRecord writer thread for as long as it stays staged.
        std::size_t max_files = 200;

        // Entries *looked at*. This is the real runaway guard -- a max_files cap
        // alone still stats every inode under `/` hunting for the 200th match.
        std::size_t max_visits = 20000;

        int max_depth = 32;

        // node_modules, build, .git and friends. Independent of `show_hidden`:
        // that rule is about what the reader can see, this one is about what the
        // walk costs, and pruning .git as a tree beats sniffing 8 KB out of
        // forty thousand loose objects the moment someone presses '.'.
        bool skip_heavy_dirs = true;

        // Polled between entries, so quitting does not wait out the walk.
        std::function<bool()> cancelled;
    };

    struct DirectoryWalk
    {
        std::vector<std::filesystem::path> files;
        bool truncated = false; ///< a cap stopped the walk; `files` is a prefix
        bool cancelled = false;
        std::size_t pruned_dirs = 0;
        // Empty on success; set only when `root` itself could not be read.
        std::string error;
    };

    // Regular files under `root`, depth-first, never following symlinks --
    // which is what makes a symlink loop impossible rather than merely
    // unlikely. A symlinked `root` is honoured (the caller pointed at it
    // deliberately); symlinks found inside the tree are skipped, so a walk can
    // never escape the directory it was given. Errors are captured, never
    // thrown, as with read_directory.
    DirectoryWalk collect_files_recursive(const std::filesystem::path &root,
                                          const WalkOptions &opts = {});

    enum class FileKind
    {
        Directory,
        Text,
        Binary,
    };

    // How much of a file is sniffed before calling it binary. Matches the
    // window `load_document` uses in suri/store/chunk_plan.cc -- the two ask
    // the same question, and answering it differently in two places is how a
    // file gets refused by one and accepted by the other.
    inline constexpr std::size_t kBinarySniffBytes = 8192;

    // Classifies an entry by a NUL-byte sniff over the first
    // `kBinarySniffBytes` of content (text has no NULs; UTF-8 text trivially
    // passes). Note this answers "is this plain text", not "can this be
    // shown" -- a PDF is correctly Binary here, and it takes a decoder, not a
    // wider sniff, to open one.
    FileKind classify_file(const std::filesystem::path &path, const FileEntry &entry);

    // The same sniff for a path already known not to be a directory, as a walk
    // produces -- a walked path has no FileEntry to hand.
    FileKind classify_file(const std::filesystem::path &path);

    struct Preview
    {
        std::string text;
        bool truncated = false;
        // Empty on success; a human-readable message on read failure.
        std::string error;
    };

    // Reads up to `max_bytes` of `path` and backs the result off to a UTF-8
    // glyph boundary so it is always safe to render. `truncated` is set when
    // the cap was hit.
    Preview read_text_preview(const std::filesystem::path &path,
                              std::size_t max_bytes = 256 * 1024);

} // namespace ftxui::ext
