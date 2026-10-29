# Windows ctypes character buffers

Character scalars now start with NUL character values. Native wide-array writes update both `.value` and indexed elements; single UTF-16 units, including NUL and surrogates, round-trip through pointer access and marshalling. Compatible arrays decay to typed pointers using their full storage, while mismatched element types fail before native dispatch.

The new Unicode-buffer fixture covers defaults, native writes, non-ASCII detection, surrogate-buffer reuse, pointer reads and typed scalar arguments. SDK5, four ctypes fixtures and the complete fixed-baseline performance gate pass. Pip26.2.1 also passes version lookup without a cache-directory override and an isolated offline wheel installation. No CPython executes. See the validation receipt and raw report alongside this document. These checks do not qualify an xMind release or installation. Earlier failed candidates remain retained in the xMind validation workspace.
