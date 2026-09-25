#pragma once

#include <cstdio>
#include <jni.h>
#include <string>

namespace eclient_runtime::debugfiles {

// Feeding the JavaVM: called once from the host bootstrap (InGameGui::setJavaVm
// already receives it). Stored in a global; all log-file creation goes through
// JNI MediaStore so logs land in the user's public Downloads folder.
void setJavaVm(JavaVM* vm);

// One open debug log file in a user-reachable location.
// Preferred location: Downloads/EClient/<name> via MediaStore (Android 10+,
// no storage permission needed). Fallbacks: raw /sdcard/Download path, then
// the game's own Android/data files dir. `where()` says which one was used.
class PublicLog {
public:
    PublicLog() = default;
    ~PublicLog();
    PublicLog(const PublicLog&) = delete;
    PublicLog& operator=(const PublicLog&) = delete;

    bool open(const char* name);      // truncates/creates fresh
    void write(const char* text);     // appends one line (+newline) and flushes
    void close();                     // flushes and clears IS_PENDING so the
                                      // file becomes visible in Downloads
    bool isOpen() const { return file_ != nullptr; }
    const std::string& where() const { return where_; }

private:
    FILE* file_ = nullptr;
    std::string contentUri_;   // set when created via MediaStore (for IS_PENDING update)
    std::string where_{"(no file)"};
};

} // namespace eclient_runtime::debugfiles
