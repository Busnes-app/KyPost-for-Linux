// libFuzzer target for readMimeBody(), the parser that reads the decrypted
// body of an OpenPGP message. That input is whatever the sender encrypted:
// the relay never saw it, so nothing upstream has checked its shape.
//
// Built only with -DKYPOST_FUZZ=ON and clang; see fuzz/CMakeLists.txt.

#include "pgp/MimeBodyReader.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    const QByteArray entity(reinterpret_cast<const char*>(data), static_cast<qsizetype>(size));
    const MimeBody body = readMimeBody(entity);

    // MimeBodyReader.h: exceeding a bound or malformed structure returns no
    // partial content, so a caller that forgets to check status still cannot
    // render half a message.
    if (body.status != MimeBody::Status::Complete && !body.isEmpty())
        std::abort();
    return 0;
}
