#pragma once

#include <string>
#include "common/types.h"

namespace Core::FileSys {

enum class FileAccess : u32 {
    Read = 0,
    Write = 1,
    ReadWrite = 2
};

class GenericHandleAllocator {
  public:
    virtual u32 requestHandle() = 0;
    virtual void releaseHandle(u32 handle) = 0;
};

class AbstractFileSystem {
  public:
    virtual bool ownsHandle(u32 handle) = 0;
    virtual u32 openFile(std::string filename, FileAccess access) = 0;
    virtual void closeFile(u32 handle) = 0;
};

} // namespace Core::FileSys
