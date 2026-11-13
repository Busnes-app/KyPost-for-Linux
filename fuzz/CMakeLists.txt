# libFuzzer targets. Off by default; needs clang:
#
#   CC=clang CXX=clang++ cmake -B build-fuzz -S . -G Ninja -DKYPOST_CORE_ONLY=ON \
#     -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=OFF -DKYPOST_FUZZ=ON
#   cmake --build build-fuzz --target MimeBodyReaderFuzzer
#   build-fuzz/fuzz/MimeBodyReaderFuzzer -max_total_time=60 fuzz/corpus/mime
#
# The parser source is compiled straight into the fuzzer so only it carries
# the fuzzer instrumentation; it needs nothing from core/ but QtCore.
if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  message(FATAL_ERROR "KYPOST_FUZZ needs clang (libFuzzer); set CXX=clang++")
endif()

add_executable(MimeBodyReaderFuzzer
  MimeBodyReaderFuzzer.cpp
  ${PROJECT_SOURCE_DIR}/core/pgp/MimeBodyReader.cpp)
target_include_directories(MimeBodyReaderFuzzer PRIVATE ${PROJECT_SOURCE_DIR}/core)
target_link_libraries(MimeBodyReaderFuzzer PRIVATE Qt${QT_VERSION_MAJOR}::Core)
target_compile_options(MimeBodyReaderFuzzer PRIVATE -fsanitize=fuzzer,address,undefined
    -fno-sanitize-recover=undefined -fno-omit-frame-pointer -g)
target_link_options(MimeBodyReaderFuzzer PRIVATE -fsanitize=fuzzer,address,undefined)
