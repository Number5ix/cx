include (${CX_TOP_SOURCE_DIR}/cmake/werror.cmake)
include (CTest)

set(tlstestsrc "")
if(CX_TLS)
    set(tlstestsrc tlstest.c tls13test.c tlsquictest.c)
endif()

# Same shape again for QUIC, which is built on cxtls and so can only be tested when it is.
set(quictestsrc "")
if(CX_QUIC)
    set(quictestsrc quictest.c quicconntest.c quicrecovtest.c quicstreamtest.c quicnettest.c)
endif()

# Same shape as the TLS tests above: every mention of httptest.c is conditional, so a
# -DCX_HTTP=OFF build is unaffected.
set(httptestsrc "")
if(CX_HTTP)
    set(httptestsrc httptest.c)
    if(CX_HTTP3)
        list(APPEND httptestsrc http3test.c)
    endif()
endif()

set(CMAKE_TESTDRIVER_BEFORE_TESTMAIN "cxTestHarnessBefore(ac, av);")
set(CMAKE_TESTDRIVER_AFTER_TESTMAIN "cxTestHarnessAfter();")

create_test_sourcelist(testsrc
    test_runner.c
    EXTRA_INCLUDE testharness.h
    debugtest.c
    cxmemtest.c
    closuretest.c
    contest.c
    digesttest.c
    fmttest.c
    parsetest.c
    logtest.c
    objtest.c
    structtest.c
    sertest.c
    sarraytest.c
    strtest.c
    strscantest.c
    httest.c
    thrtest.c
    metatest.c
    ssdtest.c
    sbtest.c
    buftest.c
    lineparsetest.c
    jsontest.c
    converttest.c
    stvartest.c
    mathtest.c
    fstest.c
    vfstest.c
    prqtest.c
    nettest.c
    ${tlstestsrc}
    ${quictestsrc}
    ${httptestsrc}
    tqtest.c
    cpptest.cpp
    alltests.c)
add_cxautogen(converttestobj.cxh
    fmttestobj.cxh
    parsetestobj.cxh
    netfilterobj.cxh
    nettestobj.cxh
    objtestobj.cxh
    tqtestobj.cxh
    vfstestprov.cxh)
set(test_runner_files
    ${testsrc}
    fmttestobj.c
    fmttestobj.cxh
    parsetestobj.c
    parsetestobj.cxh
    objtestobj.c
    objtestobj.cxh
    converttestobj.c
    converttestobj.cxh
    netfilterobj.c
    netfilterobj.cxh
    nettestobj.c
    nettestobj.cxh
    tqtestobj.c
    tqtestobj.cxh
    vfstestprov.c
    vfstestprov.cxh
    common.h
    testharness.c
    testharness.h)

if(CX_TLS)
    list(APPEND test_runner_files tlstestcert.c tlstestcert.h tls13vec.h)
endif()

if(CX_QUIC)
    list(APPEND test_runner_files quicvec.h)
endif()

source_group("" FILES ${test_runner_files})

add_executable(test_runner ${test_runner_files})
set_property(TARGET test_runner PROPERTY CXX_STANDARD 17)
set_property(TARGET test_runner PROPERTY CXX_STANDARD_REQUIRED ON)
target_include_directories(test_runner PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(test_runner cx)

if(CX_HTTP)
    target_link_libraries(test_runner cxhttp)
    # alltests.c is one translation unit listing every test by name, so it has to know at compile
    # time whether the HTTP tests were built -- same reason CX_TLS_TESTS exists below.
    target_compile_definitions(test_runner PRIVATE CX_HTTP_TESTS=1)
    if(CX_HTTP3)
        target_compile_definitions(test_runner PRIVATE CX_HTTP3_TESTS=1)
    endif()
endif()

if(CX_TLS)
    target_link_libraries(test_runner cxtls)
    target_compile_definitions(test_runner PRIVATE CX_TLS_TESTS=1)
endif()

if(CX_QUIC)
    target_link_libraries(test_runner cxquic)
    target_compile_definitions(test_runner PRIVATE CX_QUIC_TESTS=1)
endif()

if(CX_PLATFORM_IS_WASM)
    # Emscripten defaults every stack, main and pthread alike, to 64 KB, where Windows gives 1 MB
    # and Linux 8 MB. Test fixtures sized for those (quicstreamtest's SFix is ~144 KB) run off the
    # end of it, and the stack grows down into the thread's TLS block, so the first symptom is a
    # mimalloc assertion about a NULL thread heap rather than anything resembling a stack overflow.
    target_link_options(test_runner PRIVATE "-sPTHREAD_POOL_SIZE=16" "-sPROXY_TO_PTHREAD" "-sEXIT_RUNTIME"
                        "-sSTACK_SIZE=1MB" "-sDEFAULT_PTHREAD_STACK_SIZE=1MB")
endif()

set_property(TARGET test_runner PROPERTY FOLDER "tests")

add_test(NAME "\"debug: Assertions\"" COMMAND test_runner debugtest assert)
add_test(NAME "\"xalloc: Allocation\"" COMMAND test_runner cxmemtest alloc)
add_test(NAME "\"xalloc: Usable Size\"" COMMAND test_runner cxmemtest usable_size)
add_test(NAME "\"xalloc: Free\"" COMMAND test_runner cxmemtest free)
add_test(NAME "\"xalloc: Reallocation\"" COMMAND test_runner cxmemtest realloc)
add_test(NAME "\"closure: Basic Closure\"" COMMAND test_runner closuretest closure)
add_test(NAME "\"closure: Closure Chain\"" COMMAND test_runner closuretest chain)
# Each entry below runs several subtests from contest.c (grouped by feature area) in a single
# process -- see the TEST_CHAIN groups at the bottom of that file. Run
# `test_runner contest <name>` directly to invoke one subtest in isolation.
add_test(NAME "\"console: Capabilities And Writing\"" COMMAND test_runner contest grp_capsio)
add_test(NAME "\"console: Locking\"" COMMAND test_runner contest grp_locking)
add_test(NAME "\"console: Styling And Formatted Write\"" COMMAND test_runner contest grp_styling)
add_test(NAME "\"console: Cursor Control\"" COMMAND test_runner contest grp_cursor)
add_test(NAME "\"console: Input\"" COMMAND test_runner contest grp_input)
add_test(NAME "\"digest: MD5\"" COMMAND test_runner digesttest md5)
add_test(NAME "\"digest: SHA-1\"" COMMAND test_runner digesttest sha1)
add_test(NAME "\"digest: SHA-256\"" COMMAND test_runner digesttest sha256)
add_test(NAME "\"format: Integers\"" COMMAND test_runner fmttest int)
add_test(NAME "\"format: Floating Point\"" COMMAND test_runner fmttest float)
add_test(NAME "\"format: Objects\"" COMMAND test_runner fmttest object)
add_test(NAME "\"format: Pointers\"" COMMAND test_runner fmttest ptr)
add_test(NAME "\"format: Strings\"" COMMAND test_runner fmttest string)
add_test(NAME "\"format: SUID\"" COMMAND test_runner fmttest suid)
add_test(NAME "\"format: Array indexing\"" COMMAND test_runner fmttest array)
add_test(NAME "\"format: Hashtable lookup\"" COMMAND test_runner fmttest hash)
add_test(NAME "\"format: Keyed arguments\"" COMMAND test_runner fmttest keyed)
add_test(NAME "\"format: Keyed subscripting\"" COMMAND test_runner fmttest keyedsub)
add_test(NAME "\"format: Keyed stvlist lookup\"" COMMAND test_runner fmttest keyedstvl)
add_test(NAME "\"format: Keyed variant copy\"" COMMAND test_runner fmttest keyedcopy)
add_test(NAME "\"format: Defaults\"" COMMAND test_runner fmttest default)
add_test(NAME "\"format: Error handling\"" COMMAND test_runner fmttest error)
add_test(NAME "\"parse: Basics\"" COMMAND test_runner parsetest basic)
add_test(NAME "\"parse: Types\"" COMMAND test_runner parsetest types)
add_test(NAME "\"parse: Parse options\"" COMMAND test_runner parsetest opts)
add_test(NAME "\"parse: Groups\"" COMMAND test_runner parsetest group)
add_test(NAME "\"parse: Defaults\"" COMMAND test_runner parsetest default)
add_test(NAME "\"parse: Untouched destinations\"" COMMAND test_runner parsetest untouched)
add_test(NAME "\"parse: Resumable matching\"" COMMAND test_runner parsetest matchat)
add_test(NAME "\"parse: Parsable objects\"" COMMAND test_runner parsetest object)
add_test(NAME "\"parse: Error handling\"" COMMAND test_runner parsetest error)
add_test(NAME "\"log: Log Levels\"" COMMAND test_runner logtest levels)
add_test(NAME "\"log: Batch\"" COMMAND test_runner logtest batch)
add_test(NAME "\"log: Shutdown\"" COMMAND test_runner logtest shutdown)
add_test(NAME "\"log: Channels\"" COMMAND test_runner logtest channels)
add_test(NAME "\"log: Backfill\"" COMMAND test_runner logtest backfill)
add_test(NAME "\"log: Destinations\"" COMMAND test_runner logtest dest)
add_test(NAME "\"log: Channel hierarchy\"" COMMAND test_runner logtest hierarchy)
add_test(NAME "\"log: Built-in cx channel\"" COMMAND test_runner logtest cxchan)
add_test(NAME "\"log: Call sites and rate limiting\"" COMMAND test_runner logtest site)
add_test(NAME "\"log: Records and deferred formatting\"" COMMAND test_runner logtest record)
add_test(NAME "\"log: Serializers\"" COMMAND test_runner logtest serializer)
add_test(NAME "\"log: Text prefix omission\"" COMMAND test_runner logtest textomit)
add_test(NAME "\"log: Drain groups\"" COMMAND test_runner logtest groups)
add_test(NAME "\"log: Volume control\"" COMMAND test_runner logtest volume)
add_test(NAME "\"log: Boot window\"" COMMAND test_runner logtest bootwindow)
add_test(NAME "\"log: Boot replay\"" COMMAND test_runner logtest bootreplay)
add_test(NAME "\"log: Debug ring\"" COMMAND test_runner logtest debugring)
add_test(NAME "\"log: Context\"" COMMAND test_runner logtest ctx)
add_test(NAME "\"log: Console destination\"" COMMAND test_runner logtest console)
add_test(NAME "\"log: Console destination styling\"" COMMAND test_runner logtest console_style)
add_test(NAME "\"log: Wire codec\"" COMMAND test_runner logtest wire)
add_test(NAME "\"log: Wire segments\"" COMMAND test_runner logtest wiresegment)
add_test(NAME "\"log: Wire dictionary bounds\"" COMMAND test_runner logtest wiredict)
add_test(NAME "\"log: Record injection\"" COMMAND test_runner logtest inject)
add_test(NAME "\"log: Malformed wire streams\"" COMMAND test_runner logtest wirebad)
add_test(NAME "\"log: Forwarding\"" COMMAND test_runner logtest forward)
add_test(NAME "\"log: Forward spool\"" COMMAND test_runner logtest forwardspool)
add_test(NAME "\"log: Forwarder drain group\"" COMMAND test_runner logtest forwardgroup)
add_test(NAME "\"log: Forward loop prevention\"" COMMAND test_runner logtest forwardloop)
add_test(NAME "\"log: Forward loop prevention, async transport\"" COMMAND test_runner logtest forwardloopasync)
add_test(NAME "\"log: Forward boot window backfill\"" COMMAND test_runner logtest forwardboot)
add_test(NAME "\"log: Subscription\"" COMMAND test_runner logtest subscribe)
add_test(NAME "\"log: Channel catalog\"" COMMAND test_runner logtest catalog)
add_test(NAME "\"log: Destination filter and level changes\"" COMMAND test_runner logtest destfilter)
add_test(NAME "\"object: Interface\"" COMMAND test_runner objtest iface)
add_test(NAME "\"object: Class Inheritance\"" COMMAND test_runner objtest inherit)
add_test(NAME "\"object: Interface Inheritance\"" COMMAND test_runner objtest ifinherit)
add_test(NAME "\"object: Override\"" COMMAND test_runner objtest override)
add_test(NAME "\"object: Abstract Class\"" COMMAND test_runner objtest abstract)
add_test(NAME "\"object: Static Cast\"" COMMAND test_runner objtest cast)
add_test(NAME "\"object: Dynamic Cast\"" COMMAND test_runner objtest dyncast)
add_test(NAME "\"object: Object Array\"" COMMAND test_runner objtest obj_array)
add_test(NAME "\"object: Weak References\"" COMMAND test_runner objtest weakref)
add_test(NAME "\"object: ClassSet\"" COMMAND test_runner objtest classset)
add_test(NAME "\"struct: Type Identity\"" COMMAND test_runner structtest typeidentity)
add_test(NAME "\"struct: Type Nesting\"" COMMAND test_runner structtest typenesting)
add_test(NAME "\"struct: Init/Destroy\"" COMMAND test_runner structtest initdestroy)
add_test(NAME "\"struct: Copy\"" COMMAND test_runner structtest copy)
add_test(NAME "\"struct: Nested\"" COMMAND test_runner structtest nested)
add_test(NAME "\"struct: Fixed Arrays\"" COMMAND test_runner structtest fixedarray)
add_test(NAME "\"struct: Struct Pointers\"" COMMAND test_runner structtest structp)
add_test(NAME "\"struct: StructSet\"" COMMAND test_runner structtest structset)
# Each entry below runs several subtests from sertest.c (grouped by feature area) in a single
# process -- see the TEST_CHAIN groups at the bottom of that file. Run
# `test_runner sertest <name>` directly to invoke one subtest in isolation.
add_test(NAME "\"serialize: Primitives And Containers\"" COMMAND test_runner sertest grp_primitives)
add_test(NAME "\"serialize: Structs\"" COMMAND test_runner sertest grp_structs)
add_test(NAME "\"serialize: Schema And Errors\"" COMMAND test_runner sertest grp_schema)
add_test(NAME "\"serialize: JSON Format\"" COMMAND test_runner sertest grp_json)
add_test(NAME "\"serialize: Binary Format\"" COMMAND test_runner sertest grp_binary)
add_test(NAME "\"serialize: Classes\"" COMMAND test_runner sertest grp_classes)
add_test(NAME "\"serialize: References\"" COMMAND test_runner sertest grp_refs)
add_test(NAME "\"serialize: Variants\"" COMMAND test_runner sertest grp_variants)
add_test(NAME "\"sarray: Int Array\"" COMMAND test_runner sarraytest int)
add_test(NAME "\"sarray: Sorted Int Array\"" COMMAND test_runner sarraytest sorted_int)
add_test(NAME "\"sarray: Opaque Struct Array\"" COMMAND test_runner sarraytest opaque)
add_test(NAME "\"sarray: String Array\"" COMMAND test_runner sarraytest string)
add_test(NAME "\"sarray: Sort\"" COMMAND test_runner sarraytest sort)
add_test(NAME "\"sarray: String Sort\"" COMMAND test_runner sarraytest string_sort)
add_test(NAME "\"sarray: Custom Sort\"" COMMAND test_runner sarraytest custom_sort)
add_test(NAME "\"sarray: stvar Consume\"" COMMAND test_runner sarraytest stvar_consume)
add_test(NAME "\"string: Join\"" COMMAND test_runner strtest join)
add_test(NAME "\"string: Append/Prepend\"" COMMAND test_runner strtest append)
add_test(NAME "\"string: Substrings\"" COMMAND test_runner strtest substr)
add_test(NAME "\"string: Compare\"" COMMAND test_runner strtest compare)
add_test(NAME "\"string: Compare Length-Free\"" COMMAND test_runner strtest comparelen0)
add_test(NAME "\"string: Long Strings\"" COMMAND test_runner strtest longstring)
add_test(NAME "\"string: Find\"" COMMAND test_runner strtest find)
add_test(NAME "\"string: Ropes\"" COMMAND test_runner strtest rope)
add_test(NAME "\"string: Numeric Conversions\"" COMMAND test_runner strtest num)
add_test(NAME "\"string: Bytes\"" COMMAND test_runner strtest bytes)
add_test(NAME "\"string: Find Characters\"" COMMAND test_runner strtest findchar)
add_test(NAME "\"string: Trim\"" COMMAND test_runner strtest trim)
add_test(NAME "\"string: Replace\"" COMMAND test_runner strtest replace)
add_test(NAME "\"string: Split Variants\"" COMMAND test_runner strtest splitvar)
add_test(NAME "\"string: Hex\"" COMMAND test_runner strtest hex)
add_test(NAME "\"string: UTF-8\"" COMMAND test_runner strtest utf8)
add_test(NAME "\"string: Literals\"" COMMAND test_runner strtest literal)
add_test(NAME "\"strscan: Literals\"" COMMAND test_runner strscantest literal)
add_test(NAME "\"strscan: Extraction\"" COMMAND test_runner strscantest extract)
add_test(NAME "\"strscan: Typed Values\"" COMMAND test_runner strscantest typed)
add_test(NAME "\"strscan: Backtracking\"" COMMAND test_runner strscantest backtrack)
add_test(NAME "\"hashtable: Init\"" COMMAND test_runner httest init)
add_test(NAME "\"hashtable: Insert\"" COMMAND test_runner httest insert)
add_test(NAME "\"hashtable: Find\"" COMMAND test_runner httest find)
add_test(NAME "\"hashtable: Element Access\"" COMMAND test_runner httest htelem)
add_test(NAME "\"hashtable: Remove\"" COMMAND test_runner httest remove)
add_test(NAME "\"hashtable: Iterator\"" COMMAND test_runner httest iterator)
add_test(NAME "\"hashtable: Order\"" COMMAND test_runner httest order)
add_test(NAME "\"hashtable: Complex\"" COMMAND test_runner httest complex)
add_test(NAME "\"convert: Numeric Types\"" COMMAND test_runner converttest numeric)
add_test(NAME "\"convert: Strings\"" COMMAND test_runner converttest string)
add_test(NAME "\"convert: Objects\"" COMMAND test_runner converttest object)
add_test(NAME "\"stvar: SUID ownership\"" COMMAND test_runner stvartest suid)
add_test(NAME "\"stvar: Opaque ownership\"" COMMAND test_runner stvartest opaque)
add_test(NAME "\"stvar: Transient\"" COMMAND test_runner stvartest transient)
add_test(NAME "\"stvar: SSD round-trip\"" COMMAND test_runner stvartest ssd_roundtrip)
add_test(NAME "\"stvar: Nested\"" COMMAND test_runner stvartest nested)
add_test(NAME "\"thread: Basic multithreading\"" COMMAND test_runner thrtest basic)
add_test(NAME "\"thread: Futex\"" COMMAND test_runner thrtest futex)
add_test(NAME "\"thread: Futex with timeout\"" COMMAND test_runner thrtest timeout)
add_test(NAME "\"thread: Semaphore\"" COMMAND test_runner thrtest sema)
add_test(NAME "\"thread: Mutex\"" COMMAND test_runner thrtest mutex)
add_test(NAME "\"thread: Reader/writer lock\"" COMMAND test_runner thrtest rwlock)
add_test(NAME "\"thread: Event\"" COMMAND test_runner thrtest event)
add_test(NAME "\"thread: Event (with spin)\"" COMMAND test_runner thrtest event_s)
add_test(NAME "\"thread: Condition Variable\"" COMMAND test_runner thrtest condvar)
add_test(NAME "\"thread: 64-bit atomics\"" COMMAND test_runner thrtest atomic64)
add_test(NAME "\"thread: 64-bit atomics (contention)\"" COMMAND test_runner thrtest atomic64_mt)
add_test(NAME "\"meta: Block Wrapping\"" COMMAND test_runner metatest wrap)
add_test(NAME "\"meta: Protected Blocks\"" COMMAND test_runner metatest protect)
add_test(NAME "\"meta: Basic Try/Catch\"" COMMAND test_runner metatest ptry)
add_test(NAME "\"meta: Unhandled Exceptions\"" COMMAND test_runner metatest unhandled)
add_test(NAME "\"meta: Rethrow\"" COMMAND test_runner metatest rethrow)
add_test(NAME "\"meta: Cross-function Exception Handling\"" COMMAND test_runner metatest xfunc)
add_test(NAME "\"ssd: Semi-structured data tree\"" COMMAND test_runner ssdtest tree)
add_test(NAME "\"ssd: Single value root\"" COMMAND test_runner ssdtest single)
add_test(NAME "\"ssd: Subtrees\"" COMMAND test_runner ssdtest subtree)
add_test(NAME "\"ssd: Arrays\"" COMMAND test_runner ssdtest array)
add_test(NAME "\"buffer: Create\"" COMMAND test_runner buftest create)
add_test(NAME "\"buffer: Resize\"" COMMAND test_runner buftest resize)
add_test(NAME "\"buffer: Append\"" COMMAND test_runner buftest append)
add_test(NAME "\"bufchain: Basic Operations\"" COMMAND test_runner buftest chain_basic)
add_test(NAME "\"bufchain: Peek\"" COMMAND test_runner buftest chain_peek)
add_test(NAME "\"bufchain: Skip\"" COMMAND test_runner buftest chain_skip)
add_test(NAME "\"bufchain: Zero-Copy Read\"" COMMAND test_runner buftest chain_zerocopy_read)
add_test(NAME "\"bufchain: Zero-Copy Write\"" COMMAND test_runner buftest chain_zerocopy_write)
add_test(NAME "\"bufchain: Multi-Segment Operations\"" COMMAND test_runner buftest chain_multisegment)
add_test(NAME "\"bufring: Basic Operations\"" COMMAND test_runner buftest ring_basic)
add_test(NAME "\"bufring: Peek\"" COMMAND test_runner buftest ring_peek)
add_test(NAME "\"bufring: Skip\"" COMMAND test_runner buftest ring_skip)
add_test(NAME "\"bufring: Zero-Copy Read\"" COMMAND test_runner buftest ring_zerocopy_read)
add_test(NAME "\"bufring: Zero-Copy Write\"" COMMAND test_runner buftest ring_zerocopy_write)
add_test(NAME "\"bufring: Ring Buffer Wraparound\"" COMMAND test_runner buftest ring_wraparound)
add_test(NAME "\"bufring: Multi-Segment Operations\"" COMMAND test_runner buftest ring_multisegment)
add_test(NAME "\"bufring: Reserve/Commit\"" COMMAND test_runner buftest ring_reserve)
add_test(NAME "\"bufring: Reserve Across Read\"" COMMAND test_runner buftest ring_reserve_read)
add_test(NAME "\"bufchain: Gather IOV\"" COMMAND test_runner buftest chain_gather_iov)
add_test(NAME "\"bufpool: Basic Operations\"" COMMAND test_runner buftest bufpool_basic)
add_test(NAME "\"bufpool: Reuse\"" COMMAND test_runner buftest bufpool_reuse)
# Each entry below runs several subtests from sbtest.c (grouped by feature area) in a single
# process -- see the TEST_CHAIN groups at the bottom of that file. Run
# `test_runner sbtest <name>` directly to invoke one subtest in isolation.
add_test(NAME "\"streambuffer: Push And Pull\"" COMMAND test_runner sbtest grp_pushpull)
add_test(NAME "\"streambuffer: Adapters\"" COMMAND test_runner sbtest grp_adapters)
add_test(NAME "\"streambuffer: Handoff\"" COMMAND test_runner sbtest grp_handoff)
add_test(NAME "\"streambuffer: Lifecycle\"" COMMAND test_runner sbtest grp_lifecycle)
add_test(NAME "\"lineparse: Explicit EOL\"" COMMAND test_runner lineparsetest explicit)
add_test(NAME "\"lineparse: Auto EOL\"" COMMAND test_runner lineparsetest auto)
add_test(NAME "\"lineparse: Mixed EOL\"" COMMAND test_runner lineparsetest mixed)
add_test(NAME "\"lineparse: Push Mode\"" COMMAND test_runner lineparsetest push)
add_test(NAME "\"json: Parse\"" COMMAND test_runner jsontest parse)
add_test(NAME "\"json: Output\"" COMMAND test_runner jsontest out)
add_test(NAME "\"json: Tree Parse\"" COMMAND test_runner jsontest treeparse)
add_test(NAME "\"json: Tree Output\"" COMMAND test_runner jsontest treeout)
add_test(NAME "\"json: Pull Parse\"" COMMAND test_runner jsontest pullparse)
add_test(NAME "\"json: Pull Early Stop\"" COMMAND test_runner jsontest pullearlystop)
add_test(NAME "\"math: PCG Random Integer\"" COMMAND test_runner mathtest pcgint)
add_test(NAME "\"math: PCG Random Float\"" COMMAND test_runner mathtest pcgfloat)
add_test(NAME "\"math: PCG Error Condition\"" COMMAND test_runner mathtest pcgerror)
add_test(NAME "\"math: FP Comparison\"" COMMAND test_runner mathtest floatcmp)
add_test(NAME "\"fs: Path Matching\"" COMMAND test_runner fstest pathmatch)
add_test(NAME "\"fs: File Handles\"" COMMAND test_runner fstest file)
add_test(NAME "\"fs: Directory and search operations\"" COMMAND test_runner fstest ops)
# Each entry below runs several subtests from vfstest.c (grouped by feature area) in a single
# process -- see the TEST_CHAIN groups at the bottom of that file. Run
# `test_runner vfstest <name>` directly to invoke one subtest in isolation.
add_test(NAME "\"vfs: File Operations\"" COMMAND test_runner vfstest grp_fileops)
add_test(NAME "\"vfs: Mounting And Layering\"" COMMAND test_runner vfstest grp_mounting)
add_test(NAME "\"vfs: Path Resolution\"" COMMAND test_runner vfstest grp_pathresolve)
add_test(NAME "\"vfs: Caching\"" COMMAND test_runner vfstest grp_caching)
add_test(NAME "\"vfs: Copy-On-Write\"" COMMAND test_runner vfstest grp_cow)
add_test(NAME "\"vfs: Errors And Providers\"" COMMAND test_runner vfstest grp_misc)
add_test(NAME "\"prqueue: Basic Pointer-Ring Queue\"" COMMAND test_runner prqtest basic)
add_test(NAME "\"prqueue: Multithreaded Queue\"" COMMAND test_runner prqtest mt)
add_test(NAME "\"prqueue: Queue Growth\"" COMMAND test_runner prqtest grow)
add_test(NAME "\"prqueue: Garbage Collection\"" COMMAND test_runner prqtest gc)
add_test(NAME "\"prqueue: Dynamic MPMC\"" COMMAND test_runner prqtest mpmc)
# Each entry below runs several subtests from nettest.c (grouped by feature area) in a single
# process -- see the TEST_CHAIN groups at the bottom of that file. Run
# `test_runner nettest <name>` directly to invoke one subtest in isolation.
add_test(NAME "\"netflow: Lifecycle\"" COMMAND test_runner nettest grp_flowlife)
add_test(NAME "\"netflow: Reclaim And Resurrection\"" COMMAND test_runner nettest grp_flowreclaim)
add_test(NAME "\"netflow: QUIC Integration\"" COMMAND test_runner nettest grp_flowquic)
add_test(NAME "\"nettimer: Firing And Ordering\"" COMMAND test_runner nettest grp_timer)
add_test(NAME "\"netfilter: Stream Chains\"" COMMAND test_runner nettest grp_filterstream)
# Many networking features are excluded on WASM even though they're built there (see
# wasm_net_select.c): Emscripten's select()/poll() emulation under Node does not reliably
# honor the timeout or report genuine readiness for real sockets -- see
# https://github.com/emscripten-core/emscripten/issues/25759, open and unresolved upstream as of
# this writing. filter_dgram_send hits the same class of issue for a real socket round-trip. These
# test functions still compile and run manually via `test_runner nettest select_udp` etc. for
# whenever that gets fixed; they're just not part of the automated suite until then.
if(CX_PLATFORM_IS_WINDOWS OR CX_PLATFORM_IS_UNIX)
add_test(NAME "\"net: General\"" COMMAND test_runner nettest grp_general)
add_test(NAME "\"netfilter: Datagram\"" COMMAND test_runner nettest grp_filterdgram)
add_test(NAME "\"netselect: Loopback And Send\"" COMMAND test_runner nettest grp_select_loopback)
add_test(NAME "\"netselect: Connect And Accept\"" COMMAND test_runner nettest grp_select_connect)
add_test(NAME "\"nettimer: Wait Bounding\"" COMMAND test_runner nettest grp_timerwait)
if(CX_PLATFORM_IS_WINDOWS)
add_test(NAME "\"netiocp: Loopback And Send\"" COMMAND test_runner nettest grp_iocp_loopback)
add_test(NAME "\"netiocp: Connect And Accept\"" COMMAND test_runner nettest grp_iocp_connect)
endif()
if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
add_test(NAME "\"netepoll: Loopback And Backpressure\"" COMMAND test_runner nettest grp_epoll_loopback)
add_test(NAME "\"netepoll: Connect And Accept\"" COMMAND test_runner nettest grp_epoll_connect)
elseif(${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD")
add_test(NAME "\"netkqueue: Loopback And Backpressure\"" COMMAND test_runner nettest grp_kqueue_loopback)
add_test(NAME "\"netkqueue: Connect And Accept\"" COMMAND test_runner nettest grp_kqueue_connect)
endif()
endif()
if(CX_TLS)
add_test(NAME "\"tls13: HKDF-Expand-Label\"" COMMAND test_runner tls13test expandlabel)
add_test(NAME "\"tls13: Transcript Hash\"" COMMAND test_runner tls13test transcript)
add_test(NAME "\"tls13: Key Schedule\"" COMMAND test_runner tls13test schedule)
add_test(NAME "\"tls13: Key Schedule 0-RTT\"" COMMAND test_runner tls13test schedule_early)
add_test(NAME "\"tls13: ECDHE Known Answer\"" COMMAND test_runner tls13test ecdhe)
add_test(NAME "\"tls13: ECDHE Generated Keys\"" COMMAND test_runner tls13test ecdhe_generate)
add_test(NAME "\"tls13: Hello Codec\"" COMMAND test_runner tls13test hello)
add_test(NAME "\"tls13: Malformed Hello\"" COMMAND test_runner tls13test hello_malformed)

# Each entry below runs several subtests from tlsquictest.c (grouped by feature area) in a
# single process -- see the TEST_CHAIN groups at the bottom of that file. Run
# `test_runner tlsquictest <name>` directly to invoke one subtest in isolation.
add_test(NAME "\"tlsquic: Handshake\"" COMMAND test_runner tlsquictest grp_handshake)
add_test(NAME "\"tlsquic: Authentication\"" COMMAND test_runner tlsquictest grp_auth)
add_test(NAME "\"tlsquic: Malformed And Misuse\"" COMMAND test_runner tlsquictest grp_malformed)
add_test(NAME "\"tlsquic: Resumption And 0-RTT\"" COMMAND test_runner tlsquictest grp_resumption)
endif()

if(CX_QUIC)
# Each entry below runs several subtests from quictest.c (grouped by feature area) in a
# single process -- see the TEST_CHAIN groups at the bottom of that file. Run
# `test_runner quictest <name>` directly to invoke one subtest in isolation.
add_test(NAME "\"quic: Wire Format\"" COMMAND test_runner quictest grp_wireformat)
add_test(NAME "\"quic: Packet Protection\"" COMMAND test_runner quictest grp_protection)
# Each entry below runs several subtests from quicrecovtest.c (grouped by feature area) in a
# single process -- see the TEST_CHAIN groups at the bottom of that file. Run
# `test_runner quicrecovtest <name>` directly to invoke one subtest in isolation.
add_test(NAME "\"quicrecov: Send Buffering\"" COMMAND test_runner quicrecovtest grp_sendbuf)
add_test(NAME "\"quicrecov: RTT And Loss Detection\"" COMMAND test_runner quicrecovtest grp_lossdetect)
add_test(NAME "\"quicrecov: Probe Timeout\"" COMMAND test_runner quicrecovtest grp_pto)
add_test(NAME "\"quicrecov: Congestion Control\"" COMMAND test_runner quicrecovtest grp_congestion)
add_test(NAME "\"quicrecov: Persistent Congestion\"" COMMAND test_runner quicrecovtest grp_persistent)
add_test(NAME "\"quicrecov: Pacing And Discard\"" COMMAND test_runner quicrecovtest grp_pacing)

# Each entry below runs several subtests from quicstreamtest.c (grouped by feature area) in a
# single process -- see the TEST_CHAIN groups at the bottom of that file. Run
# `test_runner quicstreamtest <name>` directly to invoke one subtest in isolation.
add_test(NAME "\"quicstream: Lifecycle\"" COMMAND test_runner quicstreamtest grp_lifecycle)
add_test(NAME "\"quicstream: Data Transfer\"" COMMAND test_runner quicstreamtest grp_transfer)
add_test(NAME "\"quicstream: Flow Control\"" COMMAND test_runner quicstreamtest grp_flow)
add_test(NAME "\"quicstream: Blocked Complaints\"" COMMAND test_runner quicstreamtest grp_blocked)
add_test(NAME "\"quicstream: Retransmission\"" COMMAND test_runner quicstreamtest grp_retransmit)
add_test(NAME "\"quicstream: Reset Handling\"" COMMAND test_runner quicstreamtest grp_reset)
add_test(NAME "\"quicstream: Final Size And Errors\"" COMMAND test_runner quicstreamtest grp_finalsize)
# Each entry below runs several subtests from quicconntest.c (grouped by feature area) in a
# single process -- see the TEST_CHAIN groups at the bottom of that file. Run
# `test_runner quicconntest <name>` directly to invoke one subtest in isolation.
add_test(NAME "\"quicconn: Transport Parameters\"" COMMAND test_runner quicconntest grp_params)
add_test(NAME "\"quicconn: Handshake\"" COMMAND test_runner quicconntest grp_handshake)
add_test(NAME "\"quicconn: Frame Validation\"" COMMAND test_runner quicconntest grp_frames)
add_test(NAME "\"quicconn: Connection Lifecycle\"" COMMAND test_runner quicconntest grp_lifecycle)
add_test(NAME "\"quicconn: Path Validation And Migration\"" COMMAND test_runner quicconntest grp_path)
add_test(NAME "\"quicconn: Loss Recovery And Congestion\"" COMMAND test_runner quicconntest grp_recovery)
add_test(NAME "\"quicconn: ECN And Path MTU\"" COMMAND test_runner quicconntest grp_ecn)
add_test(NAME "\"quicconn: Early Data\"" COMMAND test_runner quicconntest grp_early)
add_test(NAME "\"quicconn: Datagrams\"" COMMAND test_runner quicconntest grp_datagram)
add_test(NAME "\"quicconn: Stream Transfer\"" COMMAND test_runner quicconntest grp_stream)
# The quicnet suite runs a real QUIC connection over loopback UDP, which WASM has nothing to run it
# on: Emscripten's sockets are WebSockets underneath and there is no datagram transport behind them.
# The quicconn suite above needs no sockets at all and runs everywhere.
if(CX_PLATFORM_IS_WINDOWS OR CX_PLATFORM_IS_UNIX)
# Each entry below runs several subtests from quicnettest.c (grouped by feature area) in a
# single process -- see the TEST_CHAIN groups at the bottom of that file. Run
# `test_runner quicnettest <name>` directly to invoke one subtest in isolation.
add_test(NAME "\"quicnet: Handshake And Streams\"" COMMAND test_runner quicnettest grp_handshake)
add_test(NAME "\"quicnet: Stream Flow Control\"" COMMAND test_runner quicnettest grp_flow)
add_test(NAME "\"quicnet: Bulk Transfer And Backpressure\"" COMMAND test_runner quicnettest grp_bulk)
add_test(NAME "\"quicnet: Connection Lifecycle\"" COMMAND test_runner quicnettest grp_lifecycle)
add_test(NAME "\"quicnet: Path And Migration\"" COMMAND test_runner quicnettest grp_path)
add_test(NAME "\"quicnet: Datagrams\"" COMMAND test_runner quicnettest grp_datagram)
add_test(NAME "\"quicnet: Accept Ordering\"" COMMAND test_runner quicnettest grp_accept)
endif()
endif()

if(CX_TLS AND (CX_PLATFORM_IS_WINDOWS OR CX_PLATFORM_IS_UNIX))
add_test(NAME "\"tls: Handshake\"" COMMAND test_runner tlstest handshake)
add_test(NAME "\"tls: Wire Is Encrypted\"" COMMAND test_runner tlstest wire)
add_test(NAME "\"tls: Certificate Verification\"" COMMAND test_runner tlstest verify)
add_test(NAME "\"tls: Hostname Verification\"" COMMAND test_runner tlstest hostname)
add_test(NAME "\"tls: Bulk Transfer\"" COMMAND test_runner tlstest large)
add_test(NAME "\"tls: STARTTLS Upgrade\"" COMMAND test_runner tlstest starttls)
add_test(NAME "\"tls: ALPN\"" COMMAND test_runner tlstest alpn)
add_test(NAME "\"tls: Mutual TLS\"" COMMAND test_runner tlstest mtls)
add_test(NAME "\"tls: Certificate Rotation\"" COMMAND test_runner tlstest rotate)
add_test(NAME "\"tls: Orderly Shutdown\"" COMMAND test_runner tlstest shutdown)
add_test(NAME "\"tls: Session Resumption\"" COMMAND test_runner tlstest resume)
add_test(NAME "\"tls: Post-Handshake Tickets\"" COMMAND test_runner tlstest tickets)
add_test(NAME "\"tls: Unusable Config Fails Closed\"" COMMAND test_runner tlstest failclosed)
add_test(NAME "\"tls: RSA Key Encodings\"" COMMAND test_runner tlstest keyformats)
endif()

add_test(NAME "\"taskqueue: Tasks\"" COMMAND test_runner tqtest task)
add_test(NAME "\"taskqueue: Failed Tasks\"" COMMAND test_runner tqtest failure)
add_test(NAME "\"taskqueue: Concurrency (in-worker)\"" COMMAND test_runner tqtest concurrency_inworker)
add_test(NAME "\"taskqueue: Concurrency (dedicated)\"" COMMAND test_runner tqtest concurrency_dedicated)
add_test(NAME "\"taskqueue: Call\"" COMMAND test_runner tqtest call)
add_test(NAME "\"taskqueue: Scheduled Tasks\"" COMMAND test_runner tqtest sched)
add_test(NAME "\"taskqueue: Monitor (in-worker)\"" COMMAND test_runner tqtest monitor_inworker)
add_test(NAME "\"taskqueue: Monitor (dedicated)\"" COMMAND test_runner tqtest monitor_dedicated)
add_test(NAME "\"taskqueue: Dependencies\"" COMMAND test_runner tqtest depend)
add_test(NAME "\"taskqueue: Require Mutex\"" COMMAND test_runner tqtest reqmutex)
add_test(NAME "\"taskqueue: Require FIFO\"" COMMAND test_runner tqtest reqfifo)
add_test(NAME "\"taskqueue: Require LIFO\"" COMMAND test_runner tqtest reqlifo)
add_test(NAME "\"taskqueue: Require Gate\"" COMMAND test_runner tqtest reqgate)
add_test(NAME "\"taskqueue: Dependency Timeout\"" COMMAND test_runner tqtest timeout)
add_test(NAME "\"taskqueue: Manual\"" COMMAND test_runner tqtest manual)
add_test(NAME "\"taskqueue: Oneshot\"" COMMAND test_runner tqtest oneshot)
add_test(NAME "\"taskqueue: Multiphase\"" COMMAND test_runner tqtest multiphase)
add_test(NAME "\"cpp: SArray\"" COMMAND test_runner cpptest sarray)
add_test(NAME "\"cpp: Hashtable\"" COMMAND test_runner cpptest hashtable)
add_test(NAME "\"cpp: stype\"" COMMAND test_runner cpptest stype)
add_test(NAME "\"cpp: stvar\"" COMMAND test_runner cpptest stvar)
add_test(NAME "\"cpp: Object\"" COMMAND test_runner cpptest object)

if(CX_HTTP AND (CX_PLATFORM_IS_WINDOWS OR CX_PLATFORM_IS_UNIX))
    # Each entry below runs several subtests from httptest.c (grouped by feature area) in a
    # single process -- see the TEST_CHAIN groups at the bottom of that file. Run
    # `test_runner httptest <name>` directly to invoke one subtest in isolation.
    add_test(NAME "\"http: URL Handling\"" COMMAND test_runner httptest grp_url)
    add_test(NAME "\"http: Headers\"" COMMAND test_runner httptest grp_headers)
    add_test(NAME "\"http: Message Parsing\"" COMMAND test_runner httptest grp_parsing)
    add_test(NAME "\"http: Encoding And Cookies\"" COMMAND test_runner httptest grp_encoding)
    add_test(NAME "\"http: Connection Exchange\"" COMMAND test_runner httptest grp_connexchange)
    add_test(NAME "\"http: Client Requests And Redirects\"" COMMAND test_runner httptest grp_clientredirect)
    add_test(NAME "\"http: Client Body And Progress\"" COMMAND test_runner httptest grp_clientbody)
    add_test(NAME "\"http: Client Pool, Cookies And Sink\"" COMMAND test_runner httptest grp_clientpool)
    add_test(NAME "\"http: Client Errors And Cancellation\"" COMMAND test_runner httptest grp_clienterrors)
    add_test(NAME "\"http: Server Basics\"" COMMAND test_runner httptest grp_srvbasics)
    add_test(NAME "\"http: Server Malformed Requests\"" COMMAND test_runner httptest grp_srvbadinput)
    add_test(NAME "\"http: Server Methods And Timing\"" COMMAND test_runner httptest grp_srvmethods)
    add_test(NAME "\"http: Server Body Handling\"" COMMAND test_runner httptest grp_srvbody)
    add_test(NAME "\"http: Server Expect-Continue\"" COMMAND test_runner httptest grp_srvcontinue)
    add_test(NAME "\"http: TLS And Round Trip\"" COMMAND test_runner httptest grp_tls)
    add_test(NAME "\"http: Multipart Uploads\"" COMMAND test_runner httptest grp_multipart)
endif()

if(CX_HTTP3)
    # The frame codec, QPACK and the Alt-Svc parser are pure functions over buffers, so these run
    # everywhere. Each entry runs several subtests from http3test.c (grouped by feature area) in
    # a single process -- see the TEST_CHAIN groups at the bottom of that file. Run
    # `test_runner http3test <name>` directly to invoke one subtest in isolation.
    add_test(NAME "\"http3: Wire And Frame Codec\"" COMMAND test_runner http3test grp_wire)
    add_test(NAME "\"http3: QPACK\"" COMMAND test_runner http3test grp_qpack)
    add_test(NAME "\"http3: Control Streams\"" COMMAND test_runner http3test grp_control)

    # The rest run a real HTTP/3 connection over loopback UDP, which WASM has nothing to run it on:
    # Emscripten's sockets are WebSockets underneath and there is no datagram transport behind them.
    # Same exclusion, and the same reason, as the quicnet suite above.
    if(CX_PLATFORM_IS_WINDOWS OR CX_PLATFORM_IS_UNIX)
        add_test(NAME "\"http3: Server Requests\"" COMMAND test_runner http3test grp_server)
        add_test(NAME "\"http3: Server Stream Control\"" COMMAND test_runner http3test grp_serverstream)
        add_test(NAME "\"http3: Client Requests\"" COMMAND test_runner http3test grp_client)
        add_test(NAME "\"http3: Dialing And Version Negotiation\"" COMMAND test_runner http3test grp_dial)

        # Left ungrouped on purpose: it runs four application threads through one shared client for
        # several seconds, so its time and its failures are worth seeing on their own.
        add_test(NAME "\"http3: Shared Client On A Task Queue\"" COMMAND test_runner http3test tqparallel)
    endif()
endif()
