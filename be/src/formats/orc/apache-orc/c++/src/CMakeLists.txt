# This file is made available under Elastic License 2.0.
# This file is based on code available under the Apache license here:
#   https://github.com/apache/orc/tree/main/c++/src/CMakeLists.txt

# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CXX11_FLAGS} ${WARN_FLAGS}")

INCLUDE(CheckCXXSourceCompiles)

CHECK_CXX_SOURCE_COMPILES("
    #include<fcntl.h>
    #include<unistd.h>
    int main(int,char*[]){
      int f = open(\"/x/y\", O_RDONLY);
      char buf[100];
      return pread(f, buf, 100, 1000) == 0;
    }"
  HAS_PREAD
)

CHECK_CXX_SOURCE_COMPILES("
    #include<time.h>
    int main(int,char*[]){
      struct tm time2020;
      return !strptime(\"2020-02-02 12:34:56\", \"%Y-%m-%d %H:%M:%S\", &time2020);
    }"
  HAS_STRPTIME
)

CHECK_CXX_SOURCE_COMPILES("
    #include<string>
    int main(int,char* argv[]){
      return static_cast<int>(std::stoll(argv[0]));
    }"
  HAS_STOLL
)

CHECK_CXX_SOURCE_COMPILES("
    #include<stdint.h>
    #include<stdio.h>
    int main(int,char*[]){
      int64_t x = 1; printf(\"%lld\",x);
    }"
  INT64_IS_LL
)

CHECK_CXX_SOURCE_COMPILES("
    #ifdef __clang__
      #pragma clang diagnostic push
      #pragma clang diagnostic ignored \"-Wdeprecated\"
      #pragma clang diagnostic pop
   #elif defined(__GNUC__)
      #pragma GCC diagnostic push
      #pragma GCC diagnostic ignored \"-Wdeprecated\"
      #pragma GCC diagnostic pop
   #elif defined(_MSC_VER)
      #pragma warning( push )
      #pragma warning( disable : 4996 )
      #pragma warning( pop )
   #else
     unknownCompiler!
   #endif
   int main(int, char *[]) {}"
  HAS_DIAGNOSTIC_PUSH
)

CHECK_CXX_SOURCE_COMPILES("
    #include<cmath>
    int main(int, char *[]) {
      return std::isnan(1.0f);
    }"
  HAS_STD_ISNAN
)

CHECK_CXX_SOURCE_COMPILES("
    #include<mutex>
    int main(int, char *[]) {
       std::mutex test_mutex;
       std::lock_guard<std::mutex> lock_mutex(test_mutex);
    }"
  HAS_STD_MUTEX
)

CHECK_CXX_SOURCE_COMPILES("
    #include<string>
    std::string func() {
      std::string var = \"test\";
      return std::move(var);
    }
    int main(int, char *[]) {}"
  NEEDS_REDUNDANT_MOVE
)

CHECK_CXX_SOURCE_COMPILES("
    #include<string>
    int main(int, char *[]) {
      double d = 5;
      std::to_string(d);
    }"
  HAS_DOUBLE_TO_STRING
)

CHECK_CXX_SOURCE_COMPILES("
    #include<cstdint>
    #include<string>
    int main(int, char *[]) {
      int64_t d = 5;
      std::to_string(d);
    }"
  HAS_INT64_TO_STRING
)

CHECK_CXX_SOURCE_COMPILES("
  constexpr int foo(int x, int y) {
    return x+y;
  }
  int main(int, char*[]) {
    return foo(1,3);
  }"
  HAS_CONSTEXPR
)

INCLUDE(CheckCXXSourceRuns)

CHECK_CXX_SOURCE_RUNS("
    #include<time.h>
    int main(int, char *[]) {
      time_t t = -14210715; // 1969-07-20 12:34:45
      struct tm *ptm = gmtime(&t);
      return !(ptm && ptm->tm_year == 69);
    }"
  HAS_PRE_1970
)

CHECK_CXX_SOURCE_RUNS("
    #include<stdlib.h>
    #include<time.h>
    int main(int, char *[]) {
      setenv(\"TZ\", \"America/Los_Angeles\", 1);
      tzset();
      struct tm time2037;
      struct tm time2038;
      strptime(\"2037-05-05 12:34:56\", \"%Y-%m-%d %H:%M:%S\", &time2037);
      strptime(\"2038-05-05 12:34:56\", \"%Y-%m-%d %H:%M:%S\", &time2038);
      return mktime(&time2038) - mktime(&time2037) != 31536000;
    }"
  HAS_POST_2038
)

set(CMAKE_REQUIRED_INCLUDES ${ZLIB_INCLUDE_DIR})
set(CMAKE_REQUIRED_LIBRARIES orc_zlib)
CHECK_CXX_SOURCE_COMPILES("
    #define Z_PREFIX
    #include<zlib.h>
    z_stream strm;
    int main(int, char *[]) {
        deflateReset(&strm);
    }"
  NEEDS_Z_PREFIX
)

configure_file (
  "Adaptor.hh.in"
  "${CMAKE_CURRENT_BINARY_DIR}/Adaptor.hh"
  )


if (DEFINED ENV{STARROCKS_HOME}) # starrocks: orc prioritize project search path
include_directories (
  BEFORE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_BINARY_DIR}
  )
else()
include_directories (
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_BINARY_DIR}
  ${LIBHDFSPP_INCLUDE_DIR}
  )
endif()

add_custom_command(OUTPUT orc_proto.pb.h orc_proto.pb.cc
   COMMAND ${PROTOBUF_EXECUTABLE}
        -I ${PROJECT_SOURCE_DIR}/proto
        --cpp_out="${CMAKE_CURRENT_BINARY_DIR}"
        "${PROJECT_SOURCE_DIR}/proto/orc_proto.proto"
)

# to generate bit_packing_gen.inc file, which is included in bit_packing.cc
add_custom_command(OUTPUT bit_packing_gen.inc
   COMMAND python ${CMAKE_CURRENT_SOURCE_DIR}/gen-bit-pack.py
)

set(SOURCE_FILES
  "${CMAKE_CURRENT_BINARY_DIR}/Adaptor.hh"
  orc_proto.pb.h
  bit_packing_gen.inc
  io/InputStream.cc
  io/OutputStream.cc
  sargs/ExpressionTree.cc
  sargs/Literal.cc
  sargs/PredicateLeaf.cc
  sargs/SargsApplier.cc
  sargs/SearchArgument.cc
  sargs/TruthValue.cc
  wrap/orc-proto-wrapper.cc
  Adaptor.cc
  BloomFilter.cc
  ByteRLE.cc
  ColumnPrinter.cc
  ColumnReader.cc
  ColumnWriter.cc
  Common.cc
  Compression.cc
  Exceptions.cc
  Int128.cc
  LzoDecompressor.cc
  MemoryPool.cc
  Murmur3.cc
  OrcFile.cc
  Reader.cc
  RLEv1.cc
  RLEV2Util.cc
  RleDecoderV2.cc
  RleEncoderV2.cc
  RLE.cc
  Statistics.cc
  StripeStream.cc
  Timezone.cc
  TypeImpl.cc
  Vector.cc
  bit_packing.cc
  Writer.cc)

if(BUILD_LIBHDFSPP)
  set(SOURCE_FILES ${SOURCE_FILES} OrcHdfsFile.cc)
  add_definitions(-DBUILD_LIBHDFSPP)
endif(BUILD_LIBHDFSPP)

add_library (orc STATIC ${SOURCE_FILES})

if (NOT DEFINED ENV{STARROCKS_HOME}) # starrocks: no need to link liborc.a with other libs and install

target_link_libraries (orc
  orc::protobuf
  orc::zlib
  orc::snappy
  orc::lz4
  orc::zstd
  ${LIBHDFSPP_LIBRARIES}
  )

install(TARGETS orc DESTINATION lib)

endif()
