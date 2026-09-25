#pragma once

#include <cstdio>
#include <jni.h>
#include <string>
#include <vector>

namespace eclient_runtime::debugfiles {

// Feeding the JavaVM: called once from the host bootstrap (InGameGui::setJavaVm
// already receives it). Stored in a global; all log-file creation goes through
// JNI MediaStore so logs land in the user's public Downloads folder.
void setJavaVm(JavaVM* vm);

// One open debug log. Writes are TEE'd to every location that could be
// opened: public Downloads via MediaStore (Android 10+, no permission), raw
// /sdcard/Download, the MC app folder (Android/data/.../files/EClient/),
// and legacy /sdcard/games/com.mojang. `where()` lists every active path.
class PublicLog {
public:
    PublicLog() = default;
    ~PublicLog();
    PublicLog(const PublicLog&) = delete;
    PublicLog& operator=(const PublicLog&) = delete;

    bool open(const char* name);      // truncates/creates fresh
    void write(const char* text);     // appends one line (+newline) to all sinks
    void close();                     // closes all; clears MediaStore IS_PENDING
    bool isOpen() const { return !files_.empty(); }
    const std::string& where() const { return where_; }

private:
    std::vector<FILE*> files_;
    std::string contentUri_;   // set when created via MediaStore (for IS_PENDING update)
    std::string where_{"(no file)"};
};

} // namespace eclient_runtime::debugfiles
