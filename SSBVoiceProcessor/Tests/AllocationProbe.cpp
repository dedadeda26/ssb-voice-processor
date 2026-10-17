#include <atomic>
#include <cstdlib>
#include <pthread.h>

// macOS test-only dyld interposition. This library is never linked into the app.
// Count allocations/frees on the thread executing the synthetic audio callback.
namespace
{
std::atomic<bool> enabled { false };
std::atomic<pthread_t> observedThread {};
std::atomic<unsigned long long> events { 0 };
void record()
{
    if (enabled.load(std::memory_order_relaxed) && pthread_self() == observedThread.load(std::memory_order_relaxed))
        events.fetch_add(1, std::memory_order_relaxed);
}
void* probeMalloc(std::size_t size) { record(); return std::malloc(size); }
void* probeCalloc(std::size_t count, std::size_t size) { record(); return std::calloc(count, size); }
void* probeRealloc(void* pointer, std::size_t size) { record(); return std::realloc(pointer, size); }
void probeFree(void* pointer) { if (pointer) record(); std::free(pointer); }
struct Interpose { const void* replacement; const void* original; };
__attribute__((used, section("__DATA,__interpose")))
const Interpose interpositions[] = {
    { (const void*)&probeMalloc, (const void*)&malloc },
    { (const void*)&probeCalloc, (const void*)&calloc },
    { (const void*)&probeRealloc, (const void*)&realloc },
    { (const void*)&probeFree, (const void*)&free }
};
}
extern "C" void ssb_begin_allocation_probe()
{
    events.store(0);
    observedThread.store(pthread_self());
    enabled.store(true);
}
extern "C" unsigned long long ssb_end_allocation_probe()
{
    enabled.store(false);
    return events.load();
}
