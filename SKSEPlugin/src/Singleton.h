#pragma once

namespace InTheirShoes {

    // Base for the plugin's one-instance classes: `class X : public Singleton<X>`, with `friend Singleton<X>`
    // and a private constructor in X
    template <class T>
    class Singleton {
    public:
        static T& GetSingleton() {
            static T instance;
            return instance;
        }

        Singleton(const Singleton&) = delete;
        Singleton& operator=(const Singleton&) = delete;

    protected:
        Singleton() = default;
        ~Singleton() = default;
    };

}
