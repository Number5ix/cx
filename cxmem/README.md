# cxmem
cxmem is a small standalone library that wraps jemalloc (or, optionally, the
system allocator). The purpose if it being separate is so that third-party
libraries can be ported to use either xaAlloc or the xa\_malloc/xa\_free
wrappers without having to depend on cx itself and causing a dependency loop
if those libraries are needed by cx.
