#pragma once
#include <string>
#include <cstdint>
#include <cstring>

namespace types {
  enum Type {
		NIL,

		// atomic types
		BOOL,
		INT,
		FLOAT,
		STRING,

		// math types
		VECTOR2,
		VECTOR2I,
		RECT2,
		RECT2I,
		VECTOR3,
		VECTOR3I,
		TRANSFORM2D,
		VECTOR4,
		VECTOR4I,
		PLANE,
		QUATERNION,
		AABB,
		BASIS,
		TRANSFORM3D,
		PROJECTION,

		// misc types
		COLOR,
		STRING_NAME,
		NODE_PATH,
		RID,
		OBJECT,
		CALLABLE,
		SIGNAL,
		DICTIONARY,
		ARRAY,

		// typed arrays
		PACKED_BYTE_ARRAY,
		PACKED_INT32_ARRAY,
		PACKED_INT64_ARRAY,
		PACKED_FLOAT32_ARRAY,
		PACKED_FLOAT64_ARRAY,
		PACKED_STRING_ARRAY,
		PACKED_VECTOR2_ARRAY,
		PACKED_VECTOR3_ARRAY,
		PACKED_COLOR_ARRAY,

		VARIANT_MAX
	};

  union MarshallFloat {
    uint32_t i; ///< int
    float f; ///< float
  };
  
  struct Vector2 {
    float x;
    float y;
  };

  unsigned int encode_vec2(Vector2 vec, std::uint8_t *bytes);
  Vector2 decode_vec2(std::uint8_t *bytes);

  unsigned int encode_uint32(uint32_t p_uint, std::uint8_t *p_arr);
  uint32_t decode_uint32(const std::uint8_t *p_arr);
  unsigned int encode_float(float p_float, std::uint8_t *p_arr);
  float decode_float(const std::uint8_t *p_arr);

	std::string vec2_to_str(Vector2 vec);
}