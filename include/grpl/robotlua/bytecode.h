#pragma once

#include <stdint.h>
#include <stddef.h>
#include <istream>

#include "smallvector.h"

// We use constexpr if, assuming it's available.
#ifndef IF_CONSTEXPR
  // C++17
  #if __cplusplus >= 201703L
    #define IF_CONSTEXPR_AVAILABLE
    #define IF_CONSTEXPR if constexpr
  #else
    #define IF_CONSTEXPR if
  #endif
#endif

namespace grpl {
namespace robotlua {

// We store Instructions and Integers as the value of size_t - 32 on 32-bit platforms and 64 on 64-bit platforms.
typedef size_t lua_instruction;
typedef int lua_integer;
typedef double lua_number;   // TODO: Add define arg for float?

struct bytecode_architecture {
  bool little;

  uint8_t sizeof_int;
  uint8_t sizeof_sizet;
  uint8_t sizeof_instruction;
  uint8_t sizeof_lua_integer;
  uint8_t sizeof_lua_number;

  static const bytecode_architecture system();
};

struct bytecode_header {
  char signature[4];
  uint8_t version;
  uint8_t format;
  char data[6];

  bytecode_architecture arch;

  lua_integer linteger;
  lua_number lnumber;
};

// TODO: Functions and Protos are circular dep?

struct bytecode_constant {
  uint8_t tag_type;
  union {
    bool value_bool;
    lua_number value_num;
    lua_integer value_int;
    small_vector<char, 32> value_str;  // TODO: small_string
  } data;
};

struct bytecode_upvalue {
  uint8_t loc;
  uint8_t idx;
};

struct bytecode_function {
  small_vector<char, 32> source; // TODO: small_string
  int line_defined;
  int last_line_defined;
  uint8_t num_params;
  uint8_t is_var_arg;
  uint8_t max_stack_size;
  /* Code */
  int num_instructions;
  small_vector<lua_instruction, 32> instructions;
  /* Constants */
  int num_constants;
  small_vector<bytecode_constant, 32> constants;
  /* Upvalues */
  int num_upvalues;
  small_vector<bytecode_upvalue, 32> upvalues;
  /* Protos */
  /* Unfortunately this is recursive, so we have to 
     have some heap allocations */
  int num_protos;
  small_vector<bytecode_function *, 32> protos;
  // Debugging information is ignored, but still must be parsed.
};

struct bytecode_chunk {
  bytecode_header header;
  uint8_t num_upvalues;
  bytecode_function root_func;
};

class bytecode_reader {
 public:
  bytecode_reader(std::istream &stream);

  void read_header(bytecode_header &header);
  void read_function(bytecode_architecture arch, bytecode_function &func);

  int read_native_int(bytecode_architecture arch);
  size_t read_sizet(bytecode_architecture arch);
  uint8_t read_byte();
  void read_block(uint8_t *out, size_t count);

  void read_lua_string(bytecode_architecture arch, base_small_vector<char> &buffer);
  lua_instruction read_lua_instruction(bytecode_architecture arch);
  lua_integer read_lua_integer(bytecode_architecture arch);
  lua_number  read_lua_number(bytecode_architecture arch);
 private:
  std::istream &_stream;
  const bytecode_architecture _sys_arch = bytecode_architecture::system();
};

} // namespace robotlua
} // namespace grpl