#ifndef FileQueue_hh
#define FileQueue_hh

#include "G4AutoLock.hh"
#include <queue>
#include <string>

/// Thread-safe FIFO queue of input file names.
/// Created and populated in main() before BeamOn.
/// Workers pop files atomically in GeneratePrimaries().
class FileQueue {
public:
    void Push(const std::string& filename) {
        G4AutoLock lock(&fMutex);
        fFiles.push(filename);
    }

    /// Pop next filename. Returns false if queue is empty.
    bool Pop(std::string& filename) {
        G4AutoLock lock(&fMutex);
        if (fFiles.empty()) return false;
        filename = fFiles.front();
        fFiles.pop();
        return true;
    }

    size_t Size() {
        G4AutoLock lock(&fMutex);
        return fFiles.size();
    }

private:
    std::queue<std::string> fFiles;
    G4Mutex fMutex = G4MUTEX_INITIALIZER;
};

#endif
