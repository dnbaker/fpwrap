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

#ifndef CONST_IF
#  if __cpp_if_constexpr
#    define CONST_IF(x) if constexpr(x)
#  else
#    define CONST_IF(x) if(x)
#  endif
#endif

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
        if(i < 0) std::fprintf(stderr, "Warning: Error code %ld when reading from gzFile\n", (long int)i);
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
        return std::is_same<PointerType, gzFile>::value;
    }
    static constexpr bool maybe_seekable() {
        return !std::is_same<PointerType, gzFile>::value;
    }
    bool seekable() const {
        CONST_IF(is_gz()) return false;
        struct stat s;
        ::fstat(::fileno(as_fp()), &s);
        return !S_ISFIFO(s.st_mode);
    }
    template<typename T>
    auto read(T &val) {
        return this->read(std::addressof(val), sizeof(T));
    }
    auto read(void *ptr, size_t nb) {
        CONST_IF(is_gz())
            return gzread(as_gz(), ptr, nb);
        else
            return std::fread(ptr, 1, nb, as_fp());
    }
    auto bulk_read(void *ptr, size_t nb) {
        CONST_IF(is_gz())
            return gzread(as_gz(), ptr, nb);
        else
            return ::read(::fileno(as_fp()), ptr, nb);
    }
    auto resize_buffer(size_t newsz) {
        CONST_IF(!is_gz()) {
            buf_.resize(newsz);
            std::setvbuf(as_fp(), buf_.data(), buf_.size());
        } else {
            gzbuffer(as_gz(), newsz);
        }
    }
    void seek(size_t pos, int mode=SEEK_SET) {
        CONST_IF(is_gz())
            gzseek(as_gz(), pos, mode);
        else
            std::fseek(as_fp(), pos, mode);
    }
    void close() {
        CONST_IF(is_gz())
            gzclose(as_gz());
        else
            fclose(as_fp());
        ptr_ = nullptr;
#if VERBOSE_AF
        std::fprintf(stderr, "Closed file at %s\n", path_.data());
#endif
        path_.clear();
    }
    auto write(const char *s) {return write(s, std::strlen(s));}
    auto write(const void *buf, size_t nelem) {
        CONST_IF(is_gz())
            return gzwrite(as_gz(), buf, nelem);
        else
            return std::fwrite(buf, 1, nelem, as_fp());
    }
    template<typename T>
    auto write(T val) {
        static constexpr bool is_char_p = std::is_same<std::decay_t<T>, char *>::value;
        CONST_IF(is_char_p) {
            CONST_IF(is_gz())
                return gzputs(as_gz(), val);
            else
                return std::fputs(val, as_fp());
        } else return this->write(&val, sizeof(val));
    }
    void open(const std::string &s, const char *mode="rb") {
        open(s.data(), mode);
    }
    int getc() {
        CONST_IF(is_gz())
            return gzgetc(as_gz());
        else
            return std::fgetc(as_fp());
    }
    void open(const char *path, const char *mode="rb") {
        if(ptr_) close();
        CONST_IF(is_gz()) {
            ptr_ = reinterpret_cast<PointerType>(gzopen(path, mode));
        } else {
            ptr_ = reinterpret_cast<PointerType>(fopen(path, mode));
        }
        if(ptr_ == nullptr)
            throw std::runtime_error(std::string("Could not open file at ") + path + " with mode" + mode);
        if(path) path_ = path;
#if VERBOSE_AF
        std::fprintf(stderr, "Opened file at path %s with mode '%s'\n", path, mode);
#endif
    }
    gzFile     as_gz() {return reinterpret_cast<gzFile>(ptr_);}
    std::FILE *as_fp() {return reinterpret_cast<std::FILE *>(ptr_);}
    int vfprintf(const char *fmt, va_list ap) {
        CONST_IF(is_gz())
            return gzvprintf(as_gz(), fmt, ap);
        else
            return std::vfprintf(as_fp(), fmt, ap);
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
        CONST_IF(is_gz())
            return gzeof(as_gz());
        else
            return std::feof(as_fp());
    }
    auto tell() const {
        CONST_IF(is_gz()) return gztell(as_gz());
        else              return std::ftell(as_fp());
    }
    ~FpWrapper() {
        if(ptr_) close();
    }
    auto       ptr()       {return ptr_;}
    const auto ptr() const {return ptr_;}
}; // FpWrapper

} // namespace util

#endif // FP_WRAP_H__
