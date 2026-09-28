/* Host build of the two uzlib checksums tinflate.c references (the device's copy of
 * uzlib ships without them; its linker drops the zlib/gzip paths that call them). */
#include <stdint.h>

uint32_t uzlib_adler32(const void* data, unsigned int length, uint32_t prev_sum) {
  const unsigned char* p = (const unsigned char*)data;
  uint32_t a = prev_sum & 0xffff;
  uint32_t b = prev_sum >> 16;
  while (length--) {
    a = (a + *p++) % 65521;
    b = (b + a) % 65521;
  }
  return (b << 16) | a;
}

uint32_t uzlib_crc32(const void* data, unsigned int length, uint32_t crc) {
  const unsigned char* p = (const unsigned char*)data;
  while (length--) {
    crc ^= *p++;
    for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }
  return crc;
}
