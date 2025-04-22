#ifndef ELAPSED_TIME_HPP
#define ELAPSED_TIME_HPP

#include <chrono>

#include <string>
#include <string_view>

class TimeElapsed {
private:
    std::chrono::time_point<std::chrono::steady_clock> start_time;
    std::string m_name;
    mutable double m_elapsed_time{0.0};

public:
    explicit TimeElapsed(std::string_view name) : start_time(std::chrono::steady_clock::now()), m_name(name) {}

    void Reset() { start_time = std::chrono::steady_clock::now(); }

    double Elapsed() const {
        auto end_time = std::chrono::steady_clock::now();
        return static_cast<double>(
                       std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count()) /
               1000.0;
    }

    std::string GetName() const { return m_name; }

    double GetElapsedTime() const { return m_elapsed_time; }

    double PrintAndRecordElapsedTime() const {
        m_elapsed_time = Elapsed();
        return m_elapsed_time;
    }
};

#endif  // ELAPSED_TIME_HPP
