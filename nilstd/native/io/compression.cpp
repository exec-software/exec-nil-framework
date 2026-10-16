#include <vector>
#include <string>
#include <cstring>
#include <zlib.h>

std::vector<unsigned char> ml_io_compression_gzip_compress(const std::vector<unsigned char>& data) {
    z_stream zs;
    memset(&zs, 0, sizeof(zs));
    if (deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 | 16, 8, Z_DEFAULT_STRATEGY) != Z_OK) return {};

    zs.next_in = (Bytef*)data.data();
    zs.avail_in = data.size();

    int ret;
    char outbuffer[32768];
    std::vector<unsigned char> outstring;

    do {
        zs.next_out = reinterpret_cast<Bytef*>(outbuffer);
        zs.avail_out = sizeof(outbuffer);
        ret = deflate(&zs, Z_FINISH);
        if (outstring.size() < zs.total_out) {
            outstring.insert(outstring.end(), outbuffer, outbuffer + zs.total_out - outstring.size());
        }
    } while (ret == Z_OK);

    deflateEnd(&zs);
    if (ret != Z_STREAM_END) return {};
    return outstring;
}

std::vector<unsigned char> ml_io_compression_gzip_decompress(  // NOLINT
    const std::vector<unsigned char>& data) {
    z_stream zs;
    memset(&zs, 0, sizeof(zs));
    if (inflateInit2(&zs, 15 | 16) != Z_OK) return {};

    zs.next_in = (Bytef*)data.data();
    zs.avail_in = data.size();

    int ret;
    char outbuffer[32768];
    std::vector<unsigned char> outstring;

    do {
        zs.next_out = reinterpret_cast<Bytef*>(outbuffer);
        zs.avail_out = sizeof(outbuffer);
        ret = inflate(&zs, Z_SYNC_FLUSH);
        if (outstring.size() < zs.total_out) {
            outstring.insert(outstring.end(), outbuffer, outbuffer + zs.total_out - outstring.size());
        }
    } while (ret == Z_OK);

    inflateEnd(&zs);
    if (ret != Z_STREAM_END && ret != Z_OK) return {};
    return outstring;
}

