///   Copyright 2016 Xma1
///
///   This file is part of NES-C#.
///
///   NES-C# is free software: you can redistribute it and/or modify
///   it under the terms of the GNU General Public License as published by
///   the Free Software Foundation, either version 3 of the License, or
///   (at your option) any later version.
///
///   NES-C# is distributed in the hope that it will be useful,
///   but WITHOUT ANY WARRANTY; without even the implied warranty of
///   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
///   See the GNU General Public License for more details.
///
///   You should have received a copy of the GNU General Public License
///   along with NES-C#. If not, see http://www.gnu.org/licenses/.
#pragma once
/// @file Profiler.h
/// @brief A tiny scoped-timer profiler (header only) to find where the emulator spends its time.
///
/// Usage: put `NES_PROFILE_SCOPE("name", a, b)` at the top of a function or block. The
/// constructor stores the start time, the destructor the end time; together with a
/// unique id, the name, the thread and two free integer values `a` and `b` (for example a
/// scanline number or a pixel position) this becomes one event on a timeline. For code that
/// runs millions of times per second (one CPU instruction) use `NES_PROFILE_HOT("name")`:
/// it only adds up total time, call count and the slowest call instead of storing events.
///
/// Nothing is recorded unless the environment variable `NES_PROFILE=<file.json>` is set (see
/// InitFromEnvironment()); when off, a scope costs one predictable branch. On exit the
/// events are written as Chrome trace JSON (open it in https://ui.perfetto.dev or
/// chrome://tracing) plus `<file>.summary.json` with the hot totals; `tools/profile_report.py`
/// turns them into a table and a timeline picture.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <unistd.h>

namespace NES
{
    namespace Profiler
    {
        /// One recorded scope.
        struct Event
        {
            uint64_t id;       ///< unique, increasing id (the "uuid" of this call)
            const char* name;  ///< function / block name (string literal)
            uint64_t startNs;  ///< start, nanoseconds since profiler start
            uint64_t endNs;    ///< end, nanoseconds since profiler start
            uint32_t thread;   ///< small thread number (see NameThread())
            int64_t a;         ///< free information, e.g. scanline
            int64_t b;         ///< free information, e.g. x position
        };

        /// Totals for a hot scope (not stored as single events).
        struct HotStat
        {
            const char* name;
            std::atomic<uint64_t> totalNs{0};
            std::atomic<uint64_t> calls{0};
            std::atomic<uint64_t> maxNs{0};
            explicit HotStat(const char* n);
        };

        /// True while recording. Read by every scope, so keep it a plain atomic.
        inline std::atomic<bool> enabled{false};

        inline std::chrono::steady_clock::time_point& Origin()
        {
            static std::chrono::steady_clock::time_point origin = std::chrono::steady_clock::now();
            return origin;
        }

        /// Nanoseconds since the profiler was started.
        inline uint64_t NowNs()
        {
            return static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - Origin()).count());
        }

        namespace detail
        {
            /// Events of one thread. Kept alive by shared_ptr so it can still be dumped after the thread ended.
            struct ThreadBuffer
            {
                std::vector<Event> events;
                uint32_t thread = 0;
                std::string name;
                uint64_t dropped = 0;
            };

            inline std::mutex& Lock() { static std::mutex m; return m; }
            inline std::vector<std::shared_ptr<ThreadBuffer>>& AllBuffers() { static std::vector<std::shared_ptr<ThreadBuffer>> v; return v; }
            inline std::vector<HotStat*>& AllHot() { static std::vector<HotStat*> v; return v; }
            inline std::string& OutputPath() { static std::string p; return p; }
            inline std::atomic<uint64_t>& NextId() { static std::atomic<uint64_t> id{1}; return id; }
            constexpr size_t kMaxEventsPerThread = 6'000'000; // ~ 290 MB worst case; then events are dropped and counted

            inline ThreadBuffer& Buffer()
            {
                thread_local std::shared_ptr<ThreadBuffer> buf = [] {
                    auto b = std::make_shared<ThreadBuffer>();
                    std::lock_guard<std::mutex> l(Lock());
                    b->thread = static_cast<uint32_t>(AllBuffers().size() + 1);
                    b->name = "thread " + std::to_string(b->thread);
                    b->events.reserve(1 << 16);
                    AllBuffers().push_back(b);
                    return b;
                }();
                return *buf;
            }
        } // namespace detail

        inline HotStat::HotStat(const char* n) : name(n)
        {
            std::lock_guard<std::mutex> l(detail::Lock());
            detail::AllHot().push_back(this);
        }

        /// Names the calling thread in the report (e.g. "cpu", "ui").
        inline void NameThread(const char* name)
        {
            if (!enabled.load(std::memory_order_relaxed))
                return;
            detail::Buffer().name = name;
        }

        /// Stores one finished event (used by Scope and for spans that do not follow a C++ scope).
        inline void Record(const char* name, uint64_t startNs, uint64_t endNs, int64_t a = 0, int64_t b = 0)
        {
            detail::ThreadBuffer& buf = detail::Buffer();
            if (buf.events.size() >= detail::kMaxEventsPerThread)
            {
                ++buf.dropped;
                return;
            }
            buf.events.push_back(Event{detail::NextId().fetch_add(1, std::memory_order_relaxed), name, startNs, endNs, buf.thread, a, b});
        }

        /// RAII timer: constructor = start, destructor = stop.
        class Scope
        {
        public:
            explicit Scope(const char* name, int64_t a = 0, int64_t b = 0)
                : name_(name), a_(a), b_(b), on_(enabled.load(std::memory_order_relaxed))
            {
                if (on_)
                    start_ = NowNs();
            }
            ~Scope()
            {
                if (on_)
                    Record(name_, start_, NowNs(), a_, b_);
            }
            Scope(const Scope&) = delete;
            Scope& operator=(const Scope&) = delete;

        private:
            const char* name_;
            int64_t a_, b_;
            bool on_;
            uint64_t start_ = 0;
        };

        /// RAII timer that only adds to a HotStat (no event per call).
        class HotScope
        {
        public:
            explicit HotScope(HotStat& stat) : stat_(stat), on_(enabled.load(std::memory_order_relaxed))
            {
                if (on_)
                    start_ = NowNs();
            }
            ~HotScope()
            {
                if (!on_)
                    return;
                uint64_t d = NowNs() - start_;
                stat_.totalNs.fetch_add(d, std::memory_order_relaxed);
                stat_.calls.fetch_add(1, std::memory_order_relaxed);
                uint64_t prev = stat_.maxNs.load(std::memory_order_relaxed);
                while (d > prev && !stat_.maxNs.compare_exchange_weak(prev, d, std::memory_order_relaxed)) {}
            }
            HotScope(const HotScope&) = delete;
            HotScope& operator=(const HotScope&) = delete;

        private:
            HotStat& stat_;
            bool on_;
            uint64_t start_ = 0;
        };

        namespace detail
        {
            inline void WriteEscaped(FILE* f, const std::string& s)
            {
                for (char c : s)
                {
                    if (c == '"' || c == '\\')
                        std::fputc('\\', f);
                    std::fputc(c, f);
                }
            }
        } // namespace detail

        /// Writes the trace and the summary. Called automatically at exit.
        inline void Dump()
        {
            if (!enabled.exchange(false))
                return;
            const std::string& path = detail::OutputPath();
            std::lock_guard<std::mutex> l(detail::Lock());
            if (std::getenv("NES_PROFILE_DEBUG"))
                for (const auto& b : detail::AllBuffers())
                    std::fprintf(stderr, "[profiler] buffer %u '%s': %zu events, %llu dropped, capacity %zu\n", b->thread, b->name.c_str(), b->events.size(), (unsigned long long)b->dropped, b->events.capacity());

            FILE* f = std::fopen(path.c_str(), "w");
            if (!f)
                return;
            std::fputs("{\"displayTimeUnit\":\"ns\",\"traceEvents\":[\n", f);
            bool first = true;
            for (const auto& buf : detail::AllBuffers())
            {
                std::fprintf(f, "%s{\"name\":\"thread_name\",\"ph\":\"M\",\"pid\":1,\"tid\":%u,\"args\":{\"name\":\"", first ? "" : ",\n", buf->thread);
                detail::WriteEscaped(f, buf->name);
                std::fputs("\"}}", f);
                first = false;
                for (const Event& e : buf->events)
                    std::fprintf(f, ",\n{\"name\":\"%s\",\"ph\":\"X\",\"pid\":1,\"tid\":%u,\"ts\":%.3f,\"dur\":%.3f,\"args\":{\"id\":%llu,\"a\":%lld,\"b\":%lld}}",
                                 e.name, e.thread, e.startNs / 1000.0, (e.endNs - e.startNs) / 1000.0,
                                 static_cast<unsigned long long>(e.id), static_cast<long long>(e.a), static_cast<long long>(e.b));
            }
            std::fputs("\n]}\n", f);
            std::fclose(f);

            FILE* s = std::fopen((path + ".summary.json").c_str(), "w");
            if (!s)
                return;
            std::fprintf(s, "{\"wall_ns\":%llu,\"hot\":[", static_cast<unsigned long long>(NowNs()));
            for (size_t i = 0; i < detail::AllHot().size(); i++)
            {
                const HotStat* h = detail::AllHot()[i];
                std::fprintf(s, "%s{\"name\":\"%s\",\"total_ns\":%llu,\"calls\":%llu,\"max_ns\":%llu}", i ? "," : "", h->name,
                             static_cast<unsigned long long>(h->totalNs.load()), static_cast<unsigned long long>(h->calls.load()),
                             static_cast<unsigned long long>(h->maxNs.load()));
            }
            uint64_t dropped = 0;
            for (const auto& buf : detail::AllBuffers())
                dropped += buf->dropped;
            std::fprintf(s, "],\"dropped_events\":%llu}\n", static_cast<unsigned long long>(dropped));
            std::fclose(s);
        }

        /// Starts recording into `path` (the trace is written by Dump(), also registered with atexit).
        inline void Start(const std::string& path)
        {
            // Create every static the dump needs *before* registering the atexit handler:
            // statics are destroyed in reverse order of construction, so anything created
            // later would already be gone when Dump() runs at exit.
            // A relative file name is taken relative to where the program was started, not to a folder
            // the program may change into later.
            std::string full = path;
            if (!full.empty() && full[0] != '/')
            {
                char cwd[4096];
                if (getcwd(cwd, sizeof(cwd)))
                    full = std::string(cwd) + "/" + full;
            }
            detail::OutputPath() = full;
            detail::Lock();
            detail::AllBuffers();
            detail::AllHot();
            detail::NextId();
            Origin();
            enabled.store(true);
            static bool registered = (std::atexit([] { Dump(); }), true);
            (void)registered;
        }

        /// Starts recording if `NES_PROFILE=<file.json>` is set.
        inline void InitFromEnvironment()
        {
            if (const char* p = std::getenv("NES_PROFILE"))
                Start(p);
        }
    } // namespace Profiler
} // namespace NES

#define NES_PROFILE_CAT2(a, b) a##b
#define NES_PROFILE_CAT(a, b) NES_PROFILE_CAT2(a, b)
/// Time the rest of the enclosing block as one event: `NES_PROFILE_SCOPE("name")` or `NES_PROFILE_SCOPE("name", scanline, x)`.
#define NES_PROFILE_SCOPE(...) ::NES::Profiler::Scope NES_PROFILE_CAT(nesProfileScope, __LINE__)(__VA_ARGS__)
/// Add up the time of the rest of the enclosing block (for very frequent code, no single events).
#define NES_PROFILE_HOT(name) \
    static ::NES::Profiler::HotStat NES_PROFILE_CAT(nesProfileHot, __LINE__)(name); \
    ::NES::Profiler::HotScope NES_PROFILE_CAT(nesProfileHotScope, __LINE__)(NES_PROFILE_CAT(nesProfileHot, __LINE__))
