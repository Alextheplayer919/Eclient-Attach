#include "bridge/DebugFiles.h"

#include <android/log.h>
#include <cstring>
#include <unistd.h>

namespace eclient_runtime::debugfiles {
namespace {
constexpr const char* TAG = "EClientDebugFiles";
JavaVM* g_vm = nullptr;

// Attach the calling thread to the JVM. Returns env; caller must call
// detach() only when it returned a fresh attach.
JNIEnv* attachEnv(bool& attached) {
    attached = false;
    if (!g_vm) return nullptr;
    JNIEnv* env = nullptr;
    if (g_vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_OK) return env;
    if (g_vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return nullptr;
    attached = true;
    return env;
}

void clearEx(JNIEnv* env) {
    if (env && env->ExceptionCheck()) env->ExceptionClear();
}

// Create a text/plain file named <name> under Download/EClient/ through
// MediaStore and return a dup()'d writable fd, or -1 on any failure.
// The created content:// uri is kept for the IS_PENDING=0 update in close().
int mediaStoreCreate(const char* name, std::string& outUri) {
    bool attached = false;
    JNIEnv* env = attachEnv(attached);
    if (!env) return -1;
    int fd = -1;

    jclass atCls = env->FindClass("android/app/ActivityThread");
    jclass cvCls = env->FindClass("android/content/ContentValues");
    jclass cvrCls = env->FindClass("android/content/ContentResolver");
    jclass pfdCls = env->FindClass("android/os/ParcelFileDescriptor");
    jclass dlCls = env->FindClass("android/provider/MediaStore$Downloads");
    jclass intCls = env->FindClass("java/lang/Integer");
    if (!atCls || !cvCls || !cvrCls || !pfdCls || !dlCls || !intCls || env->ExceptionCheck()) {
        clearEx(env);
        goto done;
    }

    {
        jobject app = env->CallStaticObjectMethod(
            atCls, env->GetStaticMethodID(atCls, "currentApplication", "()Landroid/app/Application;"));
        if (!app || env->ExceptionCheck()) { clearEx(env); goto done; }
        jobject resolver = env->CallObjectMethod(
            app, env->GetMethodID(atCls, "getContentResolver", "()Landroid/content/ContentResolver;"));
        if (!resolver || env->ExceptionCheck()) { clearEx(env); goto done; }

        jobject cv = env->NewObject(cvCls, env->GetMethodID(cvCls, "<init>", "()V"));
        jmethodID putStr = env->GetMethodID(cvCls, "put", "(Ljava/lang/String;Ljava/lang/String;)V");
        jmethodID putInt = env->GetMethodID(cvCls, "put", "(Ljava/lang/String;Ljava/lang/Integer;)V");
        jmethodID intValueOf = env->GetStaticMethodID(intCls, "valueOf", "(I)Ljava/lang/Integer;");
        if (!putStr || !putInt || !intValueOf) { clearEx(env); goto done; }

        jstring jname = env->NewStringUTF(name);
        jstring jmime = env->NewStringUTF("text/plain");
        jstring jdir = env->NewStringUTF("Download/EClient/");
        env->CallVoidMethod(cv, putStr, env->NewStringUTF("_display_name"), jname);
        env->CallVoidMethod(cv, putStr, env->NewStringUTF("mime_type"), jmime);
        env->CallVoidMethod(cv, putStr, env->NewStringUTF("relative_path"), jdir);
        jobject pending = env->CallStaticObjectMethod(intCls, intValueOf, 1);
        env->CallVoidMethod(cv, putInt, env->NewStringUTF("is_pending"), pending);

        jfieldID extField = env->GetStaticFieldID(dlCls, "EXTERNAL_CONTENT_URI", "Landroid/net/Uri;");
        jobject extUri = extField ? env->GetStaticObjectField(dlCls, extField) : nullptr;
        if (!extUri || env->ExceptionCheck()) { clearEx(env); goto done; }

        jmethodID insert = env->GetMethodID(cvrCls, "insert", "(Landroid/net/Uri;Landroid/content/ContentValues;)Landroid/net/Uri;");
        jobject fileUri = env->CallObjectMethod(resolver, insert, extUri, cv);
        if (!fileUri || env->ExceptionCheck()) { clearEx(env); goto done; }

        jclass uriCls = env->GetObjectClass(fileUri);
        jmethodID toString = env->GetMethodID(uriCls, "toString", "()Ljava/lang/String;");
        auto juri = static_cast<jstring>(env->CallObjectMethod(fileUri, toString));
        if (juri) {
            const char* c = env->GetStringUTFChars(juri, nullptr);
            if (c) { outUri = c; env->ReleaseStringUTFChars(juri, c); }
        }

        jstring jmode = env->NewStringUTF("w");
        jmethodID openFd = env->GetMethodID(cvrCls, "openFileDescriptor",
                                            "(Landroid/net/Uri;Ljava/lang/String;)Landroid/os/ParcelFileDescriptor;");
        jobject pfd = env->CallObjectMethod(resolver, openFd, fileUri, jmode);
        if (!pfd || env->ExceptionCheck()) { clearEx(env); goto done; }
        jmethodID detachFd = env->GetMethodID(pfdCls, "detachFd", "()I");
        fd = detachFd ? env->CallIntMethod(pfd, detachFd) : -1;
    }

done:
    if (env->ExceptionCheck()) env->ExceptionClear();
    if (attached) g_vm->DetachCurrentThread();
    return fd;
}

void mediaStoreClearPending(const std::string& uri) {
    if (uri.empty()) return;
    bool attached = false;
    JNIEnv* env = attachEnv(attached);
    if (!env) return;

    jclass netCls = env->FindClass("android/net/Uri");
    jclass cvCls = env->FindClass("android/content/ContentValues");
    jclass cvrCls = env->FindClass("android/content/ContentResolver");
    jclass atCls = env->FindClass("android/app/ActivityThread");
    jclass intCls = env->FindClass("java/lang/Integer");
    if (!netCls || !cvCls || !cvrCls || !atCls || !intCls) { clearEx(env); goto done; }

    {
        jstring juri = env->NewStringUTF(uri.c_str());
        jmethodID parse = env->GetStaticMethodID(netCls, "parse", "(Ljava/lang/String;)Landroid/net/Uri;");
        jobject fileUri = env->CallStaticObjectMethod(netCls, parse, juri);
        if (!fileUri || env->ExceptionCheck()) { clearEx(env); goto done; }

        jobject app = env->CallStaticObjectMethod(
            atCls, env->GetStaticMethodID(atCls, "currentApplication", "()Landroid/app/Application;"));
        jobject resolver = app ? env->CallObjectMethod(
            app, env->GetMethodID(atCls, "getContentResolver", "()Landroid/content/ContentResolver;")) : nullptr;
        if (!resolver || env->ExceptionCheck()) { clearEx(env); goto done; }

        jobject cv = env->NewObject(cvCls, env->GetMethodID(cvCls, "<init>", "()V"));
        jmethodID putInt = env->GetMethodID(cvCls, "put", "(Ljava/lang/String;Ljava/lang/Integer;)V");
        jmethodID intValueOf = env->GetStaticMethodID(intCls, "valueOf", "(I)Ljava/lang/Integer;");
        jobject zero = env->CallStaticObjectMethod(intCls, intValueOf, 0);
        env->CallVoidMethod(cv, putInt, env->NewStringUTF("is_pending"), zero);

        jmethodID update = env->GetMethodID(cvrCls, "update",
            "(Landroid/net/Uri;Landroid/content/ContentValues;Ljava/lang/String;[Ljava/lang/String;)I");
        if (update) env->CallIntMethod(resolver, update, fileUri, cv, nullptr, nullptr);
    }

done:
    if (env->ExceptionCheck()) env->ExceptionClear();
    if (attached && g_vm) g_vm->DetachCurrentThread();
}
} // namespace

void setJavaVm(JavaVM* vm) { g_vm = vm; }

PublicLog::~PublicLog() { close(); }

bool PublicLog::open(const char* name) {
    close();
    if (!name || !*name) return false;

    // 1) MediaStore → Download/EClient/<name> (survives scoped storage).
    std::string uri;
    int fd = mediaStoreCreate(name, uri);
    if (fd >= 0) {
        int dupFd = dup(fd);
        file_ = dupFd >= 0 ? fdopen(dupFd, "w") : nullptr;
        ::close(fd);
        if (file_) {
            contentUri_ = uri;
            where_ = std::string("Download/EClient/") + name;
            __android_log_print(ANDROID_LOG_INFO, TAG, "log file: %s (mediastore)", where_.c_str());
            return true;
        }
    }

    // 2) Raw public Download path (legacy-storage devices).
    std::string p2 = std::string("/sdcard/Download/EClient_") + name;
    file_ = std::fopen(p2.c_str(), "w");
    if (file_) { where_ = p2; __android_log_print(ANDROID_LOG_INFO, TAG, "log file: %s (raw)", p2.c_str()); return true; }

    // 3) The game's own external files dir.
    std::string p3 = std::string("/sdcard/Android/data/com.mojang.minecraftpe/files/") + name;
    file_ = std::fopen(p3.c_str(), "w");
    if (file_) { where_ = p3; __android_log_print(ANDROID_LOG_INFO, TAG, "log file: %s (appdir)", p3.c_str()); return true; }

    __android_log_print(ANDROID_LOG_WARN, TAG, "no writable location for %s — logcat only", name);
    return false;
}

void PublicLog::write(const char* text) {
    if (!file_ || !text) return;
    std::fprintf(file_, "%s\n", text);
    std::fflush(file_);
}

void PublicLog::close() {
    if (!file_) return;
    std::fclose(file_);
    file_ = nullptr;
    if (!contentUri_.empty()) {
        mediaStoreClearPending(contentUri_);
        contentUri_.clear();
    }
}

} // namespace eclient_runtime::debugfiles
