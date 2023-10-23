#include "common/fs_file.h"

namespace Common::FS {

File::File() = default;

File::File(const std::string& path, OpenMode mode) {
    open(path, mode);
}

File::~File() {
    close();
}

bool File::open(const std::string& path, OpenMode mode) {
    close();
    m_file = std::fopen(path.c_str(), getOpenMode(mode));
    return isOpen();
}

bool File::close() {
    if (!isOpen() || std::fclose(m_file) != 0) [[unlikely]] {
		m_file = nullptr;
		return false;
	}

	m_file = nullptr;
	return true;
}

bool File::write(std::span<const u8> data) {
    return isOpen() && std::fwrite(data.data(), 1, data.size(), m_file) == data.size();
}

bool File::read(void* data, u64 size) {
    return isOpen() && std::fread(data, 1, size, m_file) == size;
}

bool File::seek(s64 offset, SeekMode mode) {
    return isOpen() && std::fseek(m_file, offset, getSeekMode(mode)) == 0;
}

u64 File::tell() const {
    if (isOpen()) [[likely]] {
		#ifdef _WIN64
			return _ftelli64(m_file);
		#else
			return ftello64(m_file);
		#endif
	}

    return -1;
}

u64 File::getFileSize() {
#ifdef _WIN64
        const u64 pos = _ftelli64(m_file);
		if (_fseeki64(m_file, 0, SEEK_END) != 0) {
			return 0;
		}

        const u64 size = _ftelli64(m_file);
		if (_fseeki64(m_file, pos, SEEK_SET) != 0) {
			return 0;
		}
#else
        const u64 pos = ftello64(m_file);
		if (fseeko64(m_file, 0, SEEK_END) != 0) {
			return 0;
		}

        const u64 size = ftello64(m_file);
		if (fseeko64(m_file, pos, SEEK_SET) != 0) {
			return 0;
		}
#endif
	return size;
}

} // namespace Common::FS
