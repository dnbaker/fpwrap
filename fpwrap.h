#ifndef FP_WRAP_H__
#define FP_WRAP_H__
#if ZWRAP_USE_ZSTD
#  include "zstd_zlibwrapper.h"
#else
#  include <zlib.h>
#endif
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#include <sys/stat.h>

// This could certainly be extended to other compression/reading schemes, but it's not bad for now.

namespace fp {

template<typename PointerType>
inline std::uint64_t get_fsz(const char *path);

// Returns UINT64_MAX on failure, the filesize otherwise.

template<> inline std::uint64_t get_fsz<std::FILE *>(const char *path) {
    if(auto p = std::fopen(path, "rb"); p) {
        struct stat fs;
        ::fstat(::fileno(p), &fs);
        std::fclose(p);
        return fs.st_size;
    }
    return -1;
}

template<> inline std::uint64_t get_fsz<gzFile>(const char *path) {
    if(auto p = gzopen(path, "rb"); p) {
        size_t tot_read = 0;
        ssize_t i;
        char buf[1 << 15];
        while((i = gzread(p, buf, sizeof(buf))) == sizeof(buf))
            tot_read += sizeof(buf);
        if(i < 0) std::fprintf(stderr, "Warning: Error code %d when reading from gzFile\n", i);
        else tot_read += i;
        gzclose(p);
        return tot_read;
    }
    return -1;
}

template<typename PointerType>
class FpWrapper {
    PointerType ptr_;
    std::vector<char> buf_;
    std::string path_;
public:
    using type = PointerType;
    FpWrapper(type ptr=nullptr): ptr_(ptr), buf_(BUFSIZ) {}
    FpWrapper(const std::string &s, const char *m="r"): FpWrapper(s.data(), m) {}
    FpWrapper(const char *p, const char *m="r"): ptr_(nullptr), buf_(BUFSIZ) {
        this->open(p, m);
    }
    const std::string &path() const {return path_;}
    static constexpr bool is_gz() {
        return std::is_same_v<PointerType, gzFile>;
    }
    static constexpr bool maybe_seekable() {
        return !std::is_same_v<PointerType, gzFile>;
    }
    bool seekable() const {
        if constexpr(is_gz()) return false;
        struct stat s;
        ::fstat(::fileno(ptr_), &s);
        return !S_ISFIFO(s.st_mode);
    }
    template<typename T>
    auto read(T &val) {
        return this->read(std::addressof(val), sizeof(T));
    }
    auto read(void *ptr, size_t nb) {
        if constexpr(is_gz())
            return gzread(ptr_, ptr, nb);
        else
            return std::fread(ptr, 1, nb, ptr_);
    }
    auto bulk_read(void *ptr, size_t nb) {
        if constexpr(is_gz())
            return gzread(ptr_, ptr, nb);
        else
            return ::read(::fileno(ptr_), ptr, nb);
    }
    auto resize_buffer(size_t newsz) {
        if constexpr(!is_gz()) {
            buf_.resize(newsz);
            std::setvbuf(ptr_, buf_.data(), buf_.size());
        } else {
            gzbuffer(ptr_, newsz);
        }
    }
    void seek(size_t pos, int mode=SEEK_SET) {
        if constexpr(is_gz())
            gzseek(ptr_, pos, mode);
        else
            std::fseek(ptr_, pos, mode);
    }
    void close() {
        if constexpr(is_gz())
            gzclose(ptr_);
        else
            fclose(ptr_);
        ptr_ = nullptr;
#if VERBOSE_AF
        std::fprintf(stderr, "Closed file at %s\n", path_.data());
#endif
        path_.clear();
    }
    auto write(const char *s) {return write(s, std::strlen(s));}
    auto write(const void *buf, size_t nelem) {
        if constexpr(is_gz())
            return gzwrite(ptr_, buf, nelem);
        else
            return std::fwrite(buf, 1, nelem, ptr_);
    }
    template<typename T>
    auto write(T val) {
        if constexpr(std::is_same_v<std::decay_t<T>, char *>) {
            if constexpr(is_gz())
                return gzputs(ptr_, val);
            else
                return std::fputs(val, ptr_);
        } else return this->write(&val, sizeof(val));
    }
    void open(const std::string &s, const char *mode="rb") {
        open(s.data(), mode);
    }
    int getc() {
        if constexpr(is_gz())
            return gzgetc(ptr_);
        else
            return std::fgetc(ptr_);
    }
    void open(const char *path, const char *mode="rb") {
        if(ptr_) close();
        if constexpr(is_gz()) {
            ptr_ = gzopen(path, mode);
        } else {
            ptr_ = fopen(path, mode);
        }
        if(ptr_ == nullptr)
            throw std::runtime_error(std::string("Could not open file at ") + path + " with mode" + mode);
        if(path) path_ = path;
#if VERBOSE_AF
        std::fprintf(stderr, "Opened file at path %s with mode '%s'\n", path, mode);
#endif
    }
    int vfprintf(const char *fmt, va_list ap) {
        if constexpr(is_gz())
            return gzvprintf(ptr_, fmt, ap);
        else
            return std::vfprintf(ptr_, fmt, ap);
    }
    int fprintf(const char *fmt, ...) {
        va_list va;
        int ret;
        va_start(va, fmt);
        ret = this->vfprintf(fmt, va);
        va_end(va);
        return ret;
    }
    bool is_open() const {return ptr_ != nullptr;}
    auto eof() const {
        if constexpr(is_gz())
            return gzeof(ptr_);
        else
            return std::feof(ptr_);
    }
    auto tell() const {
        if constexpr(is_gz()) return gztell(ptr_);
        else                  return std::ftell(ptr_);
    }
    ~FpWrapper() {
        if(ptr_) close();
    }
    auto       ptr()       {return ptr_;}
    const auto ptr() const {return ptr_;}
}; // FpWrapper

} // namespace util

#endif // FP_WRAP_H__
