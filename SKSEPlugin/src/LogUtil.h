#pragma once

#include <mutex>
#include <optional>
#include <set>
#include <shared_mutex>

namespace InTheirShoes {

    // Logs something the first time each key is seen. Thread-safe; a key already seen costs a shared lock and a
    // lookup, so it can sit in hooks that run in parallel.
    template <class Key>
    class LogOnce {
    public:
        // True the first time a_key is passed
        bool First(const Key& a_key) {
            {
                std::shared_lock read(_lock);
                if (_seen.contains(a_key)) return false;
            }
            std::unique_lock write(_lock);
            return _seen.insert(a_key).second;
        }

    private:
        std::shared_mutex _lock;
        std::set<Key> _seen;
    };

    // Logs a value when it changes. Not thread-safe.
    template <class T>
    class LogOnChange {
    public:
        // True when a_now differs from the last value passed (or is the first since Reset)
        bool Changed(const T& a_now) {
            if (_last == a_now) return false;
            _last = a_now;
            return true;
        }
        void Reset() { _last.reset(); }

    private:
        std::optional<T> _last;
    };

}
