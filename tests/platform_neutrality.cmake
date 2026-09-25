# The platform-neutrality gate.
#
# CMakeLists.txt names the libraries that promise to call no operating system
# (NFSU_NEUTRAL_LIBRARIES) and the ones that are platform code on purpose
# (NFSU_PLATFORM_LIBRARIES). This reads every source of every neutral
# library, and every project header those sources include, however deeply,
# and fails - naming the file, the line and the symbol - on any of four
# things. docs/PLATFORM_SERVICES.md is the boundary it guards.
#
#   include  an #include of anything but a C or C++ standard header or a
#            project header. An allowlist rather than a list of platform
#            headers, because the platform headers are the set that cannot
#            be listed: <windows.h>, <unistd.h>, <switch.h>, <deko3d.h> and
#            <X11/Xlib.h> are five of hundreds, and the one a future author
#            reaches for will not be on a denylist written today. The price
#            is that a standard header this file does not know fails until
#            it is added to kStandardHeaders, and the message says so.
#
#   call     a call to a known operating-system function: POSIX file,
#            process, memory and time calls, pthread_*, the Win32 API -
#            every CamelCase call ending in A or W, and a list of the common
#            ones that end in neither - the MSVC runtime's underscore calls,
#            libnx's service prefixes and deko3d's dk*, and std::system. The
#            include rule alone is not enough, and this is the measured
#            reason: glibc's <cstdio> and <cstdlib> declare popen, fileno,
#            realpath and setenv to every C++ program, and MinGW's declare
#            _wfopen and _fileno, so a neutral file can reach the operating
#            system with nothing but standard includes.
#
#   macro    a preprocessor conditional that tests a platform - _WIN32,
#            __linux__, __APPLE__, __SWITCH__ and their kin - or probes for a
#            header with __has_include, which is how src/render.cpp picks its
#            POSIX writer. A file that asks which platform it is on is
#            platform code, whatever the branch it takes calls. A condition
#            continued onto the next line with a backslash is read there too.
#
#   clock    a read of a clock, an entropy source or the environment: a
#            std::chrono clock, time(), clock(), rand(), std::random_device,
#            getenv(). Those are the timing service's to supply. A neutral
#            library that reads one gives a different answer on every run,
#            and a recorded input test (docs/INPUT.md) stops meaning anything.
#
# Comments and the insides of string and character literals are not read: a
# line that names ::open( in a comment, or "CreateFileW(" in a message, is
# not a call. Each line is tokenised left to right - comment openers, string
# quotes and character quotes, whichever comes first - so that a quote in a
# comment and a comment opener in a literal are both read as what they are.
# That is not decoration: two headers here carry "Cars/*/GEOMETRY.BIN" inside
# a // comment, and a reader that looked for /* before it looked for // would
# take the rest of both files for a comment and pass anything below it.
#
# Every library target is classified. The manifest CMakeLists.txt writes
# lists them all, and one that is in neither list, or in both, fails here by
# name, so that a library added by any branch arrives with a decision rather
# than outside the gate.
#
# The rules are measured before the tree is read: the corpus below holds
# lines each rule must catch, lines that set traps for the tokeniser ahead of
# a line that must still be caught, and ordinary lines no rule may catch. The
# run fails if a rule has stopped firing or has started firing on ordinary
# code, because a gate whose patterns had quietly broken would otherwise pass
# every tree it was shown.
#
# What it cannot see, stated rather than left to be discovered:
#
#   - an operating-system function it does not name, reached through a
#     standard header. glibc declares hundreds; the list names the ones a
#     person is likely to reach for, and the include rule does not help
#     there, because the header is standard.
#   - a call through a pointer or a macro, or one whose name and parenthesis
#     are on different lines: a call is read as a name followed by '('.
#   - what a library links. It reads source text, so nfsu_track_render, whose
#     own source is neutral, passes although it links nfsu_render, whose
#     frame writer is not neutral.
#   - whether a classification is right. It enforces that every library has
#     one; moving a library into NFSU_PLATFORM_LIBRARIES takes it out of the
#     gate, and only review sees why.
#   - raw string literals and digit separators, which the tokeniser does not
#     parse. Neither occurs in a neutral source today. The first would have
#     its contents read as code and the second would open a character
#     literal; either gives a false finding or hides the rest of one line.
#
# Run from the build directory with SOURCE_DIR, the repository root, and
# MANIFEST, the file CMakeLists.txt writes at generate time.

cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED SOURCE_DIR OR NOT DEFINED MANIFEST)
    message(FATAL_ERROR "SOURCE_DIR and MANIFEST must both be defined")
endif()
if(NOT EXISTS "${MANIFEST}")
    message(FATAL_ERROR
        "${MANIFEST} does not exist. CMakeLists.txt writes it when the build "
        "directory is generated; run cmake on the build directory again.")
endif()

# Placeholders for the four characters CMake's list handling reads, so that a
# source splits into exactly one list element per physical line: ';'
# separates elements, an unbalanced '[' stops the splitting until its ']',
# and a '\' escapes the separator after it. Measured on a seven-line file
# whose first line held "x[": the escaping tests/path_arguments.cmake uses
# returned the whole file as one element. The bytes 0x01 to 0x04 cannot occur
# in a tracked file, because the ASCII rule in CLAUDE.md forbids them there.
string(ASCII 1 kSemicolon)
string(ASCII 2 kOpenBracket)
string(ASCII 3 kCloseBracket)
string(ASCII 4 kBackslash)

set(kStandardHeaders
    algorithm any array atomic barrier bit bitset charconv chrono codecvt
    compare complex concepts condition_variable coroutine deque exception
    execution expected filesystem flat_map flat_set format forward_list
    fstream functional future generator initializer_list iomanip ios iosfwd
    iostream istream iterator latch limits list locale map mdspan memory
    memory_resource mutex new numbers numeric optional ostream print queue
    random ranges ratio regex scoped_allocator semaphore set shared_mutex
    source_location span spanstream sstream stack stacktrace stdexcept
    stdfloat stop_token streambuf string string_view strstream syncstream
    system_error thread tuple type_traits typeindex typeinfo unordered_map
    unordered_set utility valarray variant vector version
    cassert ccomplex cctype cerrno cfenv cfloat cinttypes ciso646 climits
    clocale cmath csetjmp csignal cstdalign cstdarg cstdbool cstddef cstdint
    cstdio cstdlib cstring ctgmath ctime cuchar cwchar cwctype
    assert.h complex.h ctype.h errno.h fenv.h float.h inttypes.h iso646.h
    limits.h locale.h math.h setjmp.h signal.h stdalign.h stdarg.h
    stdbool.h stddef.h stdint.h stdio.h stdlib.h string.h tgmath.h time.h
    uchar.h wchar.h wctype.h)

# What may stand before a called name: the start of the line, or a character
# that cannot end an identifier, a member access or a qualification, then an
# optional '::' for the global namespace. So ::open( and open( are calls, and
# stream.read(, output->write( and fs::remove( are not.
set(kCallPrefix "(^|[^A-Za-z0-9_.>:])(::)?")
set(kStdPrefix "(^|[^A-Za-z0-9_.>:])((std)?::)?")

string(CONCAT kPosixCalls
    "open|openat|creat|close|read|write|pread|pwrite|readv|writev|lseek|"
    "fsync|fdatasync|ftruncate|truncate|stat|lstat|fstat|fstatat|statvfs|"
    "unlink|unlinkat|link|linkat|symlink|readlink|mkdir|mkdirat|rmdir|chdir|"
    "fchdir|getcwd|access|faccessat|chmod|fchmod|chown|umask|dup|dup2|dup3|"
    "pipe|fork|vfork|execl|execle|execlp|execv|execve|execvp|waitpid|"
    "sigaction|kill|mmap|munmap|mprotect|msync|ioctl|fcntl|isatty|ttyname|"
    "usleep|nanosleep|sleep|gettimeofday|clock_gettime|clock_nanosleep|popen|"
    "pclose|fileno|fdopen|setenv|unsetenv|putenv|realpath|mkstemp|mkdtemp|"
    "dlopen|dlsym|dlclose|dlerror|sysconf|getpid|getppid|getuid|geteuid|"
    "getgid|opendir|readdir|closedir|socket|connect|accept|listen|sendto|"
    "recvfrom|shm_open|sem_open|sched_yield|uname|posix_spawn|posix_memalign|"
    "pthread_[a-z_]+")
string(CONCAT kWin32Calls
    "GetLastError|SetLastError|CloseHandle|ReadFile|WriteFile|"
    "FlushFileBuffers|SetFileInformationByHandle|GetFileInformationByHandle|"
    "GetFileSizeEx|SetFilePointerEx|GetACP|GetOEMCP|CompareStringOrdinal|"
    "MultiByteToWideChar|WideCharToMultiByte|QueryPerformanceCounter|"
    "QueryPerformanceFrequency|GetTickCount|GetTickCount64|"
    "GetSystemTimeAsFileTime|Sleep|SleepEx|ExitProcess|TerminateProcess|"
    "GetCurrentProcessId|GetCurrentThreadId|VirtualAlloc|VirtualFree|"
    "HeapAlloc|HeapFree|LocalFree|GlobalAlloc|GlobalFree|CreateThread|"
    "WaitForSingleObject|WaitForMultipleObjects|SetEvent|ResetEvent|"
    "InitializeCriticalSection|EnterCriticalSection|LeaveCriticalSection|"
    "DeleteCriticalSection|SetCapture|ReleaseCapture|InvalidateRect|"
    "ValidateRect|BeginPaint|EndPaint|GetClientRect|AdjustWindowRect|"
    "ShowWindow|UpdateWindow|DestroyWindow|IsWindow|TranslateMessage|"
    "PostQuitMessage|SetStretchBltMode|StretchDIBits|BitBlt|"
    "CreateCompatibleDC|CreateCompatibleBitmap|SelectObject|DeleteObject|"
    "DeleteDC|GetStockObject|SetBkMode|SetTextColor|FillRect|"
    "CreateSolidBrush|GetProcAddress|FreeLibrary|XInputGetState|"
    "XInputSetState|CoInitialize|CoInitializeEx|CoCreateInstance")
string(CONCAT kCrtCalls
    "_w?(open|close|read|write|lseek|stat|fstat|stat64|stati64|fstat64|"
    "access|mkdir|rmdir|chdir|getcwd|fileno|isatty|setmode|get_osfhandle|"
    "open_osfhandle|beginthread|beginthreadex|getch|getche|kbhit|popen|"
    "pclose|fopen|putenv|dup|dup2|unlink|rename|locking|sopen|spawnl|spawnv|"
    "execl|execv|findfirst|findnext|findclose|fullpath|splitpath|makepath|"
    "chmod)")
string(CONCAT kConsoleCalls
    "(svc|hid|pad|applet|fs|fsdev|romfs|nwindow|framebuffer|console|arm|"
    "socket|nxlink|nifm|vi|nv|audren|audout|hosversion|env|thread|mutex|"
    "condvar|sm|psm|apm|hidsys|irs|account|dk)[A-Z][A-Za-z0-9]*")
string(CONCAT kClockCalls
    "time|clock|rand|srand|timespec_get|getenv|secure_getenv|_wgetenv|"
    "_dupenv_s")
string(CONCAT kClockTypes
    "system_clock|steady_clock|high_resolution_clock|file_clock|utc_clock|"
    "tai_clock|gps_clock|random_device")
string(CONCAT kPlatformMacros
    "_WIN32|_WIN64|WIN32|_WINDOWS|__WIN32__|__MINGW32__|__MINGW64__|"
    "__CYGWIN__|__linux__|__linux|linux|__gnu_linux__|__unix__|__unix|unix|"
    "__APPLE__|__MACH__|__FreeBSD__|__NetBSD__|__OpenBSD__|__DragonFly__|"
    "__ANDROID__|__SWITCH__|__NX__|__HAIKU__|__EMSCRIPTEN__|_POSIX_VERSION|"
    "__has_include")

# The code rules: the category each reports under, its pattern, the capture
# group that holds the symbol, and whether it matches a name followed by a
# parenthesis, which a declaration of that name also is. Separate variables
# rather than one list, because a pattern holds '|' and '[' and a list would
# split on them.
set(kRuleCount 11)
foreach(rule RANGE 1 ${kRuleCount})
    set(kRule${rule}_call TRUE)
endforeach()
set(kRule1_kind call)
set(kRule1_pattern "${kCallPrefix}(${kPosixCalls})[ \t]*\\(")
set(kRule1_group 3)
set(kRule2_kind call)
set(kRule2_pattern "${kCallPrefix}(${kWin32Calls})[ \t]*\\(")
set(kRule2_group 3)
set(kRule3_kind call)
set(kRule3_pattern "${kCallPrefix}([A-Z][A-Za-z0-9]*[a-z0-9][AW])[ \t]*\\(")
set(kRule3_group 3)
set(kRule4_kind call)
set(kRule4_pattern "${kCallPrefix}(${kCrtCalls})[ \t]*\\(")
set(kRule4_group 3)
set(kRule5_kind call)
set(kRule5_pattern "${kCallPrefix}(${kConsoleCalls})[ \t]*\\(")
set(kRule5_group 3)
set(kRule6_kind call)
set(kRule6_pattern "${kStdPrefix}(system)[ \t]*\\(")
set(kRule6_group 4)
set(kRule7_kind clock)
set(kRule7_pattern "${kStdPrefix}(${kClockCalls})[ \t]*\\(")
set(kRule7_group 4)
set(kRule8_kind clock)
set(kRule8_pattern "(^|[^A-Za-z0-9_])(${kClockTypes})([^A-Za-z0-9_]|$)")
set(kRule8_group 2)
set(kRule8_call FALSE)

# Rule 9: the <cstdio>/<cstdlib> file-touching functions, reached bare,
# through '::', or through 'std::' - the same three forms 'system' (rule 6)
# already allows, because these are the same kind of call: declared by a
# standard header the include rule must let in, so only the call name can
# stop them. remove and rename are the two names <cstdio> and <filesystem>
# both declare; rules 10 and 11 below catch the <filesystem> spelling.
string(CONCAT kCstdioFileCalls
    "fopen|freopen|fclose|fread|fwrite|remove|rename|tmpfile|tmpnam")
set(kRule9_kind call)
set(kRule9_pattern "${kStdPrefix}(${kCstdioFileCalls})[ \t]*\\(")
set(kRule9_group 4)

# Rules 10 and 11: the mutating std::filesystem free functions, invisible to
# every rule above once they are qualified through anything but a bare '::'
# or a literal 'std::' - the gap Finding 1 (comment 2933 on PR #136)
# describes: a project-chosen alias ('namespace fs = std::filesystem;' then
# 'fs::remove_all(...)') or the fully-spelled 'std::filesystem::remove(...)'
# (the qualifier immediately before the name is 'filesystem::', not
# 'std::'). Two rules because the two qualifier shapes need different
# prefixes: rule 10 matches the qualifier 'filesystem::' itself, with an
# optional leading 'std::'; rule 11 matches any OTHER single identifier used
# as a namespace alias in front of '::'.
#
# Rule 11 excludes the alias 'std' (and 'filesystem', so a fully-qualified
# call is not also reported through rule 11 as if 'filesystem' were a
# project alias) because 'std::copy(' is <algorithm>'s element copy, used
# throughout this codebase's neutral libraries (src/car_parts.cpp,
# src/scene.cpp, src/track_scene.cpp, src/stock_scene.cpp, src/inspect.cpp),
# and a textual rule cannot tell that overload from std::filesystem::copy,
# so it must not fire on the bare 'std::' form. std::remove and std::rename,
# the two names both headers declare, are still caught above by rule 9
# regardless.
#
# 'current_path' and 'last_write_time' mutate only when called with an
# argument (current_path) or two (last_write_time); nfsu_fs_call_mutates
# below reads what follows the opening parenthesis on the same line and, if
# it cannot tell, treats the call as a mutation - the conservative default
# this file's "what it cannot see" section already asks for.
string(CONCAT kFsMutatingCalls
    "remove_all|remove|rename|copy_file|copy_symlink|copy|"
    "create_directory_symlink|create_directories|create_directory|"
    "create_hard_link|create_symlink|resize_file|permissions|"
    "current_path|last_write_time")
set(kFsQualifiedPrefix "(^|[^A-Za-z0-9_.>:])((std::)?filesystem::)")
set(kFsAliasPrefix "(^|[^A-Za-z0-9_.>:])(([A-Za-z_][A-Za-z0-9_]*)::)")

set(kRule10_kind call)
set(kRule10_pattern "${kFsQualifiedPrefix}(${kFsMutatingCalls})[ \t]*\\(")
set(kRule10_group 4)
set(kRule10_arg_shape TRUE)

set(kRule11_kind call)
set(kRule11_pattern "${kFsAliasPrefix}(${kFsMutatingCalls})[ \t]*\\(")
set(kRule11_group 4)
set(kRule11_exclude_group 3)
set(kRule11_exclude_list "std;filesystem")
set(kRule11_arg_shape TRUE)

set(kRuleText_include "includes a header that is neither C or C++ standard nor this project's")
set(kRuleText_call "calls the operating system")
set(kRuleText_macro "tests which platform it is built for")
set(kRuleText_clock "reads a clock, an entropy source or the environment")

# Whether a matched current_path/last_write_time call, given what follows
# its opening parenthesis on this line, is the mutating overload rather
# than the read-only one: current_path() with nothing before the closing
# parenthesis is the getter; last_write_time(path) with no comma before its
# closing parenthesis is the one-argument getter. Every other name in
# kFsMutatingCalls mutates regardless of its arguments, and a call this
# cannot resolve on one line is treated as a mutation - the same
# conservative default.
function(nfsu_fs_call_mutates symbol after out_var)
    set(mutates TRUE)
    if(symbol STREQUAL "current_path")
        if(after MATCHES "^[ \t]*\\)")
            set(mutates FALSE)
        endif()
    elseif(symbol STREQUAL "last_write_time")
        if(after MATCHES "^[^,()]*\\)")
            set(mutates FALSE)
        endif()
    endif()
    set(${out_var} ${mutates} PARENT_SCOPE)
endfunction()

# Text split into one list element per physical line, with the placeholders
# above standing in for the characters a list would read.
function(nfsu_split_lines text out_var)
    string(REPLACE "\\" "${kBackslash}" text "${text}")
    string(REPLACE ";" "${kSemicolon}" text "${text}")
    string(REPLACE "[" "${kOpenBracket}" text "${text}")
    string(REPLACE "]" "${kCloseBracket}" text "${text}")
    string(REGEX REPLACE "\r?\n" ";" lines "${text}")
    set(${out_var} "${lines}" PARENT_SCOPE)
endfunction()

# The placeholders turned back into what they stand for. Only for text about
# to be printed: a restored ';' or '[' inside a list element would split it
# or merge it with the next.
function(nfsu_restore text out_var)
    string(REPLACE "${kSemicolon}" ";" text "${text}")
    string(REPLACE "${kOpenBracket}" "[" text "${text}")
    string(REPLACE "${kCloseBracket}" "]" text "${text}")
    string(REPLACE "${kBackslash}" "\\" text "${text}")
    set(${out_var} "${text}" PARENT_SCOPE)
endfunction()

# Whether the code before a matched name makes it a declaration rather than a
# call: a name preceded by another identifier, as in "void write(" or
# "bool read(", is being declared or defined, and a method may be named like
# an operating-system call. The words that can stand before a call in an
# expression are the exception, so "return write(" is still a call.
function(nfsu_declared before out_var)
    set(declared FALSE)
    if(before MATCHES "([A-Za-z_][A-Za-z0-9_]*)[ \t*&]+$")
        if(NOT CMAKE_MATCH_1 MATCHES
           "^(return|throw|else|do|case|co_return|co_yield|co_await|not|and|or|xor|sizeof|alignof|typeid|decltype|noexcept)$")
            set(declared TRUE)
        endif()
    endif()
    set(${out_var} ${declared} PARENT_SCOPE)
endfunction()

# One line of C++, with the comment state it starts in, returned with every
# comment removed and every string and character literal emptied.
function(nfsu_strip_line line in_block out_code out_block)
    set(code "")
    set(rest "${line}")
    while(NOT rest STREQUAL "")
        if(in_block)
            string(FIND "${rest}" "*/" close)
            if(close LESS 0)
                set(rest "")
                break()
            endif()
            math(EXPR after "${close} + 2")
            string(SUBSTRING "${rest}" ${after} -1 rest)
            set(in_block FALSE)
            string(APPEND code " ")
            continue()
        endif()
        string(REGEX MATCH "^[^/\"']+" plain "${rest}")
        if(NOT plain STREQUAL "")
            string(APPEND code "${plain}")
            string(LENGTH "${plain}" skip)
            string(SUBSTRING "${rest}" ${skip} -1 rest)
            continue()
        endif()
        string(SUBSTRING "${rest}" 0 2 head)
        if(head STREQUAL "//")
            break()
        endif()
        if(head STREQUAL "/*")
            string(SUBSTRING "${rest}" 2 -1 rest)
            set(in_block TRUE)
            continue()
        endif()
        string(SUBSTRING "${rest}" 0 1 first)
        if(first STREQUAL "/")
            string(APPEND code "/")
            string(SUBSTRING "${rest}" 1 -1 rest)
            continue()
        endif()
        # A literal, closed by the next quote of its kind that is not
        # escaped. One left open runs to the end of the line.
        string(REGEX MATCH "^${first}([^${first}${kBackslash}]|${kBackslash}.)*${first}"
               literal "${rest}")
        string(APPEND code "${first}${first}")
        if(literal STREQUAL "")
            set(rest "")
            break()
        endif()
        string(LENGTH "${literal}" skip)
        string(SUBSTRING "${rest}" ${skip} -1 rest)
    endwhile()
    set(${out_code} "${code}" PARENT_SCOPE)
    set(${out_block} "${in_block}" PARENT_SCOPE)
endfunction()

# Reads one file's text. Returns its findings, two list elements per finding
# - "  <label>:<line>: <symbol> - <rule>" and the code it is on - and the
# project headers it includes, relative to SOURCE_DIR. `dir` is where a quoted
# include outside include/nfsu is looked for.
function(nfsu_scan text label dir out_findings out_includes out_lines)
    nfsu_split_lines("${text}" lines)
    set(findings "")
    set(includes "")
    set(number 0)
    set(in_block FALSE)
    set(continued FALSE)
    foreach(line IN LISTS lines)
        math(EXPR number "${number} + 1")
        set(code "${line}")
        if(in_block OR line MATCHES "[/\"']")
            nfsu_strip_line("${line}" ${in_block} code in_block)
        endif()
        set(hits "")
        if(code MATCHES "^[ \t]*#[ \t]*include")
            if(line MATCHES "#[ \t]*include[ \t]*([<\"])([^>\"]*)[>\"]")
                set(delimiter "${CMAKE_MATCH_1}")
                set(header "${CMAKE_MATCH_2}")
                if(delimiter STREQUAL "<")
                    if(NOT header IN_LIST kStandardHeaders)
                        list(APPEND hits "include|<${header}>")
                    endif()
                elseif(header MATCHES "^nfsu/" AND EXISTS "${SOURCE_DIR}/include/${header}")
                    list(APPEND includes "include/${header}")
                elseif(NOT header STREQUAL "" AND EXISTS "${SOURCE_DIR}/${dir}/${header}" AND
                       NOT IS_DIRECTORY "${SOURCE_DIR}/${dir}/${header}")
                    file(RELATIVE_PATH local "${SOURCE_DIR}" "${SOURCE_DIR}/${dir}/${header}")
                    list(APPEND includes "${local}")
                else()
                    list(APPEND hits "include|\"${header}\", which is not in this repository")
                endif()
            else()
                list(APPEND hits "include|an #include this gate cannot read")
            endif()
        endif()
        set(conditional FALSE)
        if(continued OR
           code MATCHES "^[ \t]*#[ \t]*(if|ifdef|ifndef|elif|elifdef|elifndef)([^A-Za-z0-9_]|$)")
            set(conditional TRUE)
            if(code MATCHES "(^|[^A-Za-z0-9_])(${kPlatformMacros})([^A-Za-z0-9_]|$)")
                list(APPEND hits "macro|${CMAKE_MATCH_2}")
            endif()
        endif()
        set(continued FALSE)
        if(conditional AND line MATCHES "${kBackslash}[ \t]*$")
            set(continued TRUE)
        endif()
        foreach(rule RANGE 1 ${kRuleCount})
            # Every match on the line, left to right, until one is a call:
            # a declaration of the name is skipped and the rest of the line
            # read, so "void write(int) { write(x); }" is still caught. A
            # match whose qualifier is excluded (rule 11's 'std'/'filesystem'
            # aliases) or whose argument shape says it does not mutate
            # (rule 10/11's current_path/last_write_time) is skipped the
            # same way.
            set(remainder "${code}")
            while(remainder MATCHES "${kRule${rule}_pattern}")
                set(whole "${CMAKE_MATCH_0}")
                set(symbol "${CMAKE_MATCH_${kRule${rule}_group}}")
                set(lead "${CMAKE_MATCH_1}${CMAKE_MATCH_2}")
                string(FIND "${remainder}" "${whole}" at)
                string(LENGTH "${lead}" lead_length)
                math(EXPR name_at "${at} + ${lead_length}")
                string(SUBSTRING "${remainder}" 0 ${name_at} before)
                string(LENGTH "${whole}" whole_length)
                math(EXPR next "${at} + ${whole_length}")
                set(declared FALSE)
                if(kRule${rule}_call)
                    nfsu_declared("${before}" declared)
                endif()
                set(excluded FALSE)
                if(NOT declared AND DEFINED kRule${rule}_exclude_group)
                    set(qualifier "${CMAKE_MATCH_${kRule${rule}_exclude_group}}")
                    if(qualifier IN_LIST kRule${rule}_exclude_list)
                        set(excluded TRUE)
                    endif()
                endif()
                set(mutates TRUE)
                if(NOT declared AND NOT excluded AND kRule${rule}_arg_shape)
                    string(SUBSTRING "${remainder}" ${next} -1 after)
                    nfsu_fs_call_mutates("${symbol}" "${after}" mutates)
                endif()
                if(NOT declared AND NOT excluded AND mutates)
                    list(APPEND hits "${kRule${rule}_kind}|${symbol}")
                    break()
                endif()
                string(SUBSTRING "${remainder}" ${next} -1 remainder)
            endwhile()
        endforeach()
        if(NOT hits STREQUAL "")
            string(STRIP "${line}" shown)
            foreach(hit IN LISTS hits)
                string(FIND "${hit}" "|" bar)
                string(SUBSTRING "${hit}" 0 ${bar} kind)
                math(EXPR after "${bar} + 1")
                string(SUBSTRING "${hit}" ${after} -1 symbol)
                list(APPEND findings "  ${label}:${number}: ${symbol} - ${kRuleText_${kind}}")
                list(APPEND findings "      ${shown}")
            endforeach()
        endif()
    endforeach()
    set(${out_findings} "${findings}" PARENT_SCOPE)
    set(${out_includes} "${includes}" PARENT_SCOPE)
    set(${out_lines} "${number}" PARENT_SCOPE)
endfunction()

# ---------------------------------------------------------------------------
# The rules, measured before they are trusted
# ---------------------------------------------------------------------------

# Every line here must be caught, except a comment line and the first half of
# a continued conditional, which are there to set up the line after them.
# The last four pairs are traps for the tokeniser: if it misreads the first
# line of a pair, the violation on the second is read as a comment or a
# literal and goes missing, and the check below names it.
set(must_catch [==[
#include <windows.h>
#  include <unistd.h>
#include <sys/stat.h>
#include <switch.h>
#include <deko3d.h>
#include "windows.h"
    const int fd = ::open(path, O_RDONLY);
    if (fsync(fd) != 0) {
    const char* name = realpath(path, nullptr);
    pthread_mutex_lock(&mutex);
    HANDLE handle = CreateFileW(name, GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    const DWORD code = GetLastError();
    const UINT page = ::GetACP();
    FILE* file = _wfopen(name, L"rb");
    padUpdate(&pad);
    while (appletMainLoop()) {
    dkQueueSubmitCommands(queue, list);
    std::system("ls");
#ifdef _WIN32
#if defined(__linux__) || defined(__APPLE__)
#elif __has_include(<unistd.h>)
#if defined(NFSU_SOMETHING) || \
    defined(__SWITCH__)
    const auto now = std::chrono::steady_clock::now();
    std::random_device entropy;
    const char* home = std::getenv("HOME");
    std::srand(static_cast<unsigned>(std::time(nullptr)));
    const std::clock_t spent = clock();
    return write(descriptor, data, size);
    void flush() { write(descriptor, data, size); }
// Cars/*/GEOMETRY.BIN, a block opener inside a line comment
#include <windows.h>
    const char* url = "http://example/*"; ::close(descriptor);
    if (ch == '"') ::close(descriptor); // a quote in a character literal
    if (ch == '\\') ::close(descriptor); /* a backslash in one */ x = GetTickCount();
    /* a block comment that ends */ ::unlink(path);
    FILE* handle = fopen(name.c_str(), "rb");
    FILE* reopened = freopen(name.c_str(), "rb", stdin);
    std::fclose(handle);
    std::fread(buffer, 1, size, handle);
    std::fwrite(buffer, 1, size, handle);
    std::remove(temp_path.c_str());
    std::rename(old_path.c_str(), new_path.c_str());
    std::tmpfile();
    std::tmpnam(buffer);
    std::filesystem::remove(target);
    std::filesystem::remove_all(directory);
    std::filesystem::rename(old_path, new_path);
    std::filesystem::copy(source_dir, dest_dir);
    std::filesystem::copy_file(source, target);
    std::filesystem::copy_symlink(source, target);
    std::filesystem::create_directory(output_dir);
    std::filesystem::create_directories(output_dir);
    std::filesystem::create_hard_link(existing, link_path);
    std::filesystem::create_symlink(target, link_path);
    std::filesystem::create_directory_symlink(target, link_path);
    std::filesystem::resize_file(target, 0);
    std::filesystem::permissions(target, perms);
    std::filesystem::current_path(next_dir);
    std::filesystem::last_write_time(target, stamp, write_error);
// namespace fs = std::filesystem; assumed already in scope
    fs::remove_all(directory);
    fs::remove(path, error);
    fs::rename(old_path, new_path);
    fs::copy(source_dir, dest_dir);
]==])

# No line here may be caught: ordinary code this repository already holds,
# and near misses for every rule.
set(must_pass [==[
#include "nfsu/chunk.h"
#include <cstdio>
#include <filesystem>
    stream.read(reinterpret_cast<char*>(data.data()), size);
    output->write(bytes, count);
    const std::filesystem::path path(text);
    std::filesystem::current_path();
    fs::current_path();
    const auto stamp = std::filesystem::last_write_time(target);
    const auto stamp2 = fs::last_write_time(target);
    std::copy(values.begin(), values.end(), out.begin());
    session.rename(new_name);
    exporter->remove_all();
    static bool remove(const Item& item);
    if (byte >= 0x20U && byte <= 0x7EU && ch != '"' && ch != '\\') {
    // #include <windows.h>, ::open( and _WIN32, all in a comment
    error = "CreateFileW( failed: \" #ifdef _WIN32 \" and getenv(";
    const std::uint64_t time_us = frame.time_us;
    std::uint64_t now_us() override;
    link_count(nodes);
    const int wait = sleep_budget;
    std::array<std::uint8_t, 4> quad[2];
    return desktop_file_system();
    const double operand = frame.random_seed;
#ifndef NFSU_INSPECT_H
#if NFSU_WRITER_POSIX
    Inspector inspector;
    GamepadFrame frame = next_frame();
    ReadMe(value);
    virtual void write(LogLevel level, const std::string& line) = 0;
    bool read(unsigned char* data, std::size_t size) override;
    static int close(Handle& handle) noexcept;
    void FakeLogging::write(LogLevel level, const std::string& line) {
]==])

nfsu_scan("${must_catch}" "must_catch" "tests" catch_findings ignored catch_lines)
set(caught "")
foreach(finding IN LISTS catch_findings)
    if(finding MATCHES "^  must_catch:([0-9]+): ")
        list(APPEND caught "${CMAKE_MATCH_1}")
    endif()
endforeach()
list(REMOVE_DUPLICATES caught)
nfsu_split_lines("${must_catch}" catch_list)
set(missed "")
set(expected 0)
set(number 0)
foreach(line IN LISTS catch_list)
    math(EXPR number "${number} + 1")
    if(line STREQUAL "" OR line MATCHES "^//" OR line MATCHES "${kBackslash}[ \t]*$")
        continue()
    endif()
    math(EXPR expected "${expected} + 1")
    if(NOT number IN_LIST caught)
        string(STRIP "${line}" shown)
        list(APPEND missed "  must_catch:${number}: ${shown}")
    endif()
endforeach()
if(NOT missed STREQUAL "")
    string(REPLACE ";" "\n" listing "${missed}")
    nfsu_restore("${listing}" listing)
    message(FATAL_ERROR
        "the gate's rules no longer catch lines they exist to catch:\n${listing}\n"
        "A rule that has stopped firing passes every tree, so this is checked "
        "before any source is read. Fix the pattern or the tokeniser, not the "
        "corpus.")
endif()

nfsu_scan("${must_pass}" "must_pass" "tests" pass_findings ignored pass_lines)
nfsu_split_lines("${must_pass}" pass_list)
set(ordinary 0)
foreach(line IN LISTS pass_list)
    if(NOT line STREQUAL "")
        math(EXPR ordinary "${ordinary} + 1")
    endif()
endforeach()
if(NOT pass_findings STREQUAL "")
    string(REPLACE ";" "\n" listing "${pass_findings}")
    nfsu_restore("${listing}" listing)
    message(FATAL_ERROR
        "the gate's rules fire on ordinary code:\n${listing}\n"
        "A gate that reports ordinary code is a gate someone deletes. Narrow "
        "the pattern that fired.")
endif()

# ---------------------------------------------------------------------------
# The classification: every library in exactly one list
# ---------------------------------------------------------------------------

file(READ "${MANIFEST}" manifest_text)
nfsu_split_lines("${manifest_text}" manifest_lines)
set(libraries "")
set(neutral "")
set(platform "")
set(exempt "")
foreach(entry IN LISTS manifest_lines)
    if(entry MATCHES "^library ([^ ]+)$")
        list(APPEND libraries "${CMAKE_MATCH_1}")
    elseif(entry MATCHES "^neutral ([^ ]+) ?(.*)$")
        list(APPEND neutral "${CMAKE_MATCH_1}")
        set(sources_of_${CMAKE_MATCH_1} "${CMAKE_MATCH_2}")
    elseif(entry MATCHES "^platform ([^ ]+)$")
        list(APPEND platform "${CMAKE_MATCH_1}")
    elseif(entry MATCHES "^exempt (.+)$")
        list(APPEND exempt "${CMAKE_MATCH_1}")
    elseif(NOT entry STREQUAL "")
        message(FATAL_ERROR "${MANIFEST} holds a line this gate cannot read: ${entry}")
    endif()
endforeach()
if(libraries STREQUAL "")
    message(FATAL_ERROR "${MANIFEST} names no library, so there is nothing to classify")
endif()

# Every library in exactly one list, and every classified name a library.
function(nfsu_classify libraries neutral platform out_var)
    set(problems "")
    foreach(library IN LISTS libraries)
        set(in_neutral FALSE)
        set(in_platform FALSE)
        if(library IN_LIST neutral)
            set(in_neutral TRUE)
        endif()
        if(library IN_LIST platform)
            set(in_platform TRUE)
        endif()
        if(NOT in_neutral AND NOT in_platform)
            list(APPEND problems "  ${library} is in neither list")
        elseif(in_neutral AND in_platform)
            list(APPEND problems "  ${library} is in both lists")
        endif()
    endforeach()
    foreach(library IN LISTS neutral platform)
        if(NOT library IN_LIST libraries)
            list(APPEND problems "  ${library} is classified and is not a library target")
        endif()
    endforeach()
    set(${out_var} "${problems}" PARENT_SCOPE)
endfunction()

# Measured first, on lists made up to hold each mistake once: a check that
# only a bad tree exercises would otherwise break unnoticed until one came.
nfsu_classify("lib_a;lib_b;lib_c;lib_d" "lib_a;lib_c;lib_gone" "lib_c;lib_d" measured)
set(expected_problems
    "  lib_b is in neither list"
    "  lib_c is in both lists"
    "  lib_gone is classified and is not a library target")
if(NOT measured STREQUAL "${expected_problems}")
    string(REPLACE ";" "\n" listing "${measured}")
    message(FATAL_ERROR
        "the classification check no longer names what it exists to name. On "
        "made-up lists it reported:\n${listing}")
endif()

nfsu_classify("${libraries}" "${neutral}" "${platform}" unclassified)
if(NOT unclassified STREQUAL "")
    string(REPLACE ";" "\n" listing "${unclassified}")
    nfsu_restore("${listing}" listing)
    message(FATAL_ERROR
        "every library must be classified exactly once:\n${listing}\n"
        "Add it to NFSU_NEUTRAL_LIBRARIES in CMakeLists.txt if it calls no "
        "operating system - this gate will then read it - or to "
        "NFSU_PLATFORM_LIBRARIES with a comment saying what it calls and why "
        "that cannot sit behind a service in include/nfsu/platform.h.")
endif()

# ---------------------------------------------------------------------------
# The sources, then every project header they reach
# ---------------------------------------------------------------------------

set(queue "")
set(source_count 0)
set(exempted "")
foreach(library IN LISTS neutral)
    string(REPLACE "|" ";" sources "${sources_of_${library}}")
    set(read_here 0)
    foreach(source IN LISTS sources)
        if(IS_ABSOLUTE "${source}")
            file(RELATIVE_PATH source "${SOURCE_DIR}" "${source}")
        endif()
        if(NOT source MATCHES "\\.(c|cc|cpp|cxx|h|hh|hpp|hxx|inl)$")
            continue()
        endif()
        if(source IN_LIST exempt)
            list(APPEND exempted "${source} (${library})")
            continue()
        endif()
        if(NOT EXISTS "${SOURCE_DIR}/${source}")
            message(FATAL_ERROR
                "${library} lists ${source}, which does not exist under ${SOURCE_DIR}")
        endif()
        if(NOT source IN_LIST queue)
            list(APPEND queue "${source}")
            set(owner_${source} "${library}")
        endif()
        math(EXPR read_here "${read_here} + 1")
    endforeach()
    # A neutral library of which the gate reads nothing would pass whatever
    # it held.
    if(read_here EQUAL 0)
        message(FATAL_ERROR
            "${library} is in NFSU_NEUTRAL_LIBRARIES and the manifest gives it no "
            "source this gate can read, so it would pass whatever it held")
    endif()
    math(EXPR source_count "${source_count} + ${read_here}")
endforeach()

list(LENGTH queue sources_queued)
set(findings "")
set(finding_count 0)
set(line_count 0)
set(index 0)
list(LENGTH queue queued)
while(index LESS queued)
    list(GET queue ${index} path)
    math(EXPR index "${index} + 1")
    file(READ "${SOURCE_DIR}/${path}" text)
    get_filename_component(dir "${path}" DIRECTORY)
    nfsu_scan("${text}" "${path}" "${dir}" file_findings includes lines)
    math(EXPR line_count "${line_count} + ${lines}")
    if(NOT file_findings STREQUAL "")
        list(LENGTH file_findings length)
        math(EXPR finding_count "${finding_count} + ${length} / 2")
        list(APPEND findings "${file_findings}")
        # The library a source belongs to, or the file that reached a
        # header, so that a finding in a header says why it was read.
        list(APPEND findings "      in ${owner_${path}}")
    endif()
    foreach(header IN LISTS includes)
        if(NOT header IN_LIST queue)
            list(APPEND queue "${header}")
            set(owner_${header} "a header included by ${path}")
        endif()
    endforeach()
    list(LENGTH queue queued)
endwhile()

# The headers read are everything the queue held beyond the sources, counted
# from the queue itself rather than from where headers are found, so that a
# header found and never queued is not counted as read.
math(EXPR header_count "${queued} - ${sources_queued}")

# Every neutral source here includes a project header. None read means the
# include scan has stopped queueing them, and every header - the one place a
# platform include can hide from a source - would go unread.
if(header_count EQUAL 0)
    message(FATAL_ERROR
        "the neutral sources reached no project header, so the include scan has "
        "stopped following them and no header was read")
endif()

if(NOT findings STREQUAL "")
    string(REPLACE ";" "\n" listing "${findings}")
    nfsu_restore("${listing}" listing)
    message(FATAL_ERROR
        "${finding_count} finding(s) in code that promises to call no operating "
        "system:\n${listing}\n"
        "Each finding names the file, the line and the symbol, then quotes the "
        "code. A neutral library reaches the platform only through a service "
        "in include/nfsu/platform.h (docs/PLATFORM_SERVICES.md): take the "
        "service as a parameter instead of calling the platform. A standard "
        "header this gate does not know belongs in kStandardHeaders above. If "
        "the library really is platform code, move it from "
        "NFSU_NEUTRAL_LIBRARIES to NFSU_PLATFORM_LIBRARIES in CMakeLists.txt "
        "and say why there.")
endif()

list(LENGTH neutral neutral_count)
list(LENGTH caught caught_count)
string(REPLACE ";" ", " platform_text "${platform}")
string(REPLACE ";" ", " exempt_text "${exempted}")
if(exempt_text STREQUAL "")
    set(exempt_text "none on this platform")
endif()
message(STATUS
    "platform neutrality: the rules caught ${caught_count} of ${expected} "
    "corpus lines and none of ${ordinary} ordinary ones. Read "
    "${neutral_count} neutral libraries - ${source_count} sources and "
    "${header_count} project headers, ${line_count} lines - with no finding. "
    "Not read: ${platform_text}, the platform libraries, and the exempt "
    "sources: ${exempt_text}.")
