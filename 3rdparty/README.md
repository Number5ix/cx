# Notes on third-party software
## jemalloc
jemalloc is included in order to have a high-performance, highly concurrent,
low fragmentation allocator available regardless of what the underlying
operating system provides. While cxmem can be configured to use the system
allocator instead, the default configuration of using jemalloc is highly
recommended.

cx's fork of jemalloc more tightly integrates it with cxmem and the cx build
and runtime configuration options. It also adds support for memory profiling
on Windows platforms using the native NT kernel stacktrace facilities, as
well as the background thread for memory reclamation, which is pthread-only
in the official version.

## mbedTLS
Currently cx only uses a small portion of mbedTLS (entropy gathering), but
it is planned to leverage much more of the library for hashing, crypto, etc.
