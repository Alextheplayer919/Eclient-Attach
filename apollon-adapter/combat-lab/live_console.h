#pragma once
// Small bounded, thread-safe event buffer; never stores game pointers or names.
#include <array>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>
namespace eclient_console {
class Log {
    std::mutex mutex_;
    std::array<std::string, 100> entries_{};
    size_t start_=0, count_=0;
    unsigned long long generation_=0;
public:
    void add(const char* format, ...) {
        char text[240];
        va_list args;va_start(args,format);std::vsnprintf(text,sizeof(text),format,args);va_end(args);
        auto seconds=std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        char line[280];std::snprintf(line,sizeof(line),"[%lld] %s",static_cast<long long>(seconds),text);
        std::lock_guard<std::mutex> lock(mutex_);
        size_t index=(start_+count_)%entries_.size();
        if(count_==entries_.size()){index=start_;start_=(start_+1)%entries_.size();}else ++count_;
        entries_[index]=line;++generation_;
    }
    void clear() {std::lock_guard<std::mutex> lock(mutex_);start_=count_=0;++generation_;}
    std::vector<std::string> snapshot(unsigned long long& generation) {
        std::lock_guard<std::mutex> lock(mutex_);generation=generation_;
        std::vector<std::string> out;out.reserve(count_);
        for(size_t i=0;i<count_;++i)out.push_back(entries_[(start_+i)%entries_.size()]);
        return out;
    }
};
static Log log;
}
