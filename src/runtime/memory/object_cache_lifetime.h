#pragma once

namespace xlang3::memory {
// Trivial TLS survives cache destructors. Global runtimes may release objects
// afterwards, when recycling must bypass the destroyed thread-local caches.
inline thread_local bool object_caches_alive = true;
}
