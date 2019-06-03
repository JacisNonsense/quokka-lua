#pragma once

#include "bytecode.h"
#include "smallvector.h"

#include <functional>

namespace quokka {
namespace engine {

  #define CALL_STATUS_LUA (1 << 1)
  #define CALL_STATUS_FRESH (1 << 3)
  #define CALL_STATUS_TAIL (1 << 5)

  const int MULTIRET = -1;

  struct lua_call {
    // Index of the function in the stack
    size_t func_idx;
    // Function type specific info
    union {
      struct {
        size_t base;
        const lua_instruction *pc;
      } lua;
      struct { } native;
    } info;
    // How many results (return vals) from this function?
    int numresults = 0; // TODO: Check negatives
    unsigned callstatus = 0;
  };

  // Our equivilent of lua_state
  class quokka_vm {
   public:
    quokka_vm();
    quokka_vm(bytecode_chunk &bc) : quokka_vm() {
      load(bc);
    }
    
    void load(bytecode_chunk &);

    object_store_ref alloc_object();
    upval_ref alloc_upval();

    /**
     * Call a function that has its closure on the register stack already
     */
    void call(size_t nargs = 0, int nreturn = 0);

    // To be called from within a called function
    // Gets a local variable passed to a function. ID starts from 1
    lua_value &argument(int id);
    int num_params();
    void push(const lua_value &v);
    void push_global(const lua_value &key) {
      push(env().get(key));
    }
    lua_value &pop();
    void pop(size_t num);

    lua_table &env();
    object_store_ref alloc_native_function(lua_native_closure::func_t f);
    void define_native_function(const lua_value &key, lua_native_closure::func_t f);
    
   private:
    using call_ref = continuous_reference<lua_call>;
    using reg_ref = continuous_reference<lua_value>;

    // Return true if C function
    bool precall(size_t func_stack_idx, int nreturn);
    void execute();
    // Return false if multi results (variable number)
    bool postcall(size_t first_result_idx, int nreturn);

    void close_upvals(size_t level);
    object_store_ref lclosure_cache(bytecode_prototype &proto, size_t func_base, object_store_ref parent_cl);
    object_store_ref lclosure_new(bytecode_prototype &proto, size_t func_base, object_store_ref parent_cl);

    /**
     * Find the next available 'slot' in a vector that contains single-element
     * variants.
     */
    template<typename T, size_t S>
    size_t first_avail_idx(small_vector<simple_variant<T>, S> &v) {
      for (size_t i = 0; i < v.size(); i++) {
        if (!v[i].is_assigned()) return i;
      }
      // Create a new slot if one doesn't exist.
      // Safe to emplace since we have to call another emplace on the actual 
      // variant.
      size_t slot = v.size();
      v.emplace_back();
      return slot;
    }

    small_vector<lua_value, 48> _registers;
    small_vector<lua_call, 16> _callinfo;
    // Upval storage - used for variables that transcend the normal scope. 
    // e.g. local variables in ownership by an anonymous function
    small_vector<lua_upval, 16> _upvals;
    // Store for objects
    small_vector<lua_object, 16> _objects;

    /**
     * In Lua, all loaded files have a single upvalue - the _ENV (environment).
     * Unless otherwise specified, Lua sets _ENV to the 'distinguished environment'
     * (also called _G in legacy Lua). All variables, e.g. foo, in the loaded file 
     * are actually _ENV.foo.
     * 
     * See http://lua-users.org/lists/lua-l/2014-08/msg00345.html
     * 
     * For simplicity, we choose to always use the distinguished env as _ENV. Cases
     * where different _ENVs are required should be fulfilled with multiple instances
     * of vm.
     */
    lua_value _distinguished_env;
  };
}
}