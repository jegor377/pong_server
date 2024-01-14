#include "types.hpp"

namespace types {
  unsigned int encode_uint32(uint32_t p_uint, uint8_t *p_arr) {
    for (int i = 0; i < 4; i++) {
      *p_arr = p_uint & 0xFF;
      p_arr++;
      p_uint >>= 8;
    }

    return sizeof(uint32_t);
  }

  uint32_t decode_uint32(const uint8_t *p_arr) {
    uint32_t u = 0;

    for (int i = 0; i < 4; i++) {
      uint32_t b = *p_arr;
      b <<= (i * 8);
      u |= b;
      p_arr++;
    }

    return u;
  }

  unsigned int encode_float(float p_float, uint8_t *p_arr) {
    MarshallFloat mf;
    mf.f = p_float;
    encode_uint32(mf.i, p_arr);

    return sizeof(uint32_t);
  }

  float decode_float(const uint8_t *p_arr) {
    MarshallFloat mf;
    mf.i = decode_uint32(p_arr);
    return mf.f;
  }
  
  // bytes has to be size 12 -> [type:4, x:4, y:4]
  unsigned int encode_vec2(Vector2 vec, uint8_t *bytes) {
    *reinterpret_cast<int*>(&bytes[0]) = VECTOR2;
    *reinterpret_cast<Vector2*>(&bytes[4]) = vec;
    return sizeof(Vector2);
  }

  Vector2 decode_vec2(uint8_t *bytes) {
    return *reinterpret_cast<Vector2*>(&bytes[4]);
  }

  std::string vec2_to_str(Vector2 vec) {
    return "{x = " + std::to_string(vec.x) + ", y = " + std::to_string(vec.y) + "}";
  }
}