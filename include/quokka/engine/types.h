#pragma once

#include <stdint.h>
#include <functional>

#include "view.h"
#include "smallstring.h"

namespace quokka {
namespace engine {
  using lua_instruction = size_t;
  using lua_integer     = int;
  using lua_number      = double;
  using lua_string      = small_string<16>;
  using lua_nil         = std::monostate;

  /**
   * The Quokka Lua Tag Type is a simplified version of the PUC-RIO Tag Type.
   * The original version has two sections: The Tag and the Variant, with the
   * variant describing subtypes (e.g. float/integer numbers, lua/native functions).
   * We do not require variant, as we can derive that from `simple_variant`.
   * 
   * The Tag Type simply gives the 'overall type' of a value (see: lua_value).
   */
  enum class lua_tag_type {
    NIL = 0,
    BOOL = 1, 
    // light_user_data ignored - in quokka, we only have user_data
    NUMBER = 3, // Note: internally, NUMBER can be either a float or an integer internally. See lua_value for info.
    STRING = 4,
    TABLE = 5,
    FUNC = 6,    // Note: internally, FUNC can be either a Lua closure or a Native closure. See lua_object for info.
    USER_DATA = 7,
    // thread ignored
    OBJECT = 100  // Objects are never committed to bytecode, but to retain constexpr get_tag_type, we need to let the user handle their own indirection
  };

  /**
   * Tag types in bytecode have variant information. We don't actually care about that, since we use simplevariant,
   * so we ignore it.
   */
  inline lua_tag_type trunc_tag_type(uint8_t bc_tagtype) {
    return (lua_tag_type)(bc_tagtype & 0x0F);
  }

  // Fwd decl lua_object and lua_upval
  struct lua_object;
  struct lua_upval;

  using object_view = small_vector_refcount_view<lua_object>;

  /**
   * lua_value is the main container for data in Lua, containing the value of any variables
   * used in the program.
   * 
   * lua_value is polymorphic, in similar representation to a C-style union due to the use of
   * simple_variant. Because of this, all lua_values are the same size, regardless of the 
   * data they hold. 
   */
  using lua_value = std::variant<lua_nil, bool, lua_number, lua_integer, lua_string, object_view, void *>;
  
  using upval_variant_t = std::variant<lua_nil, size_t, lua_value>;

  using upval_view = small_vector_refcount_view<lua_upval>;

  /* Fwd Decls */
  struct quokka_vm;
  struct bytecode_prototype;

  /**
   * lua_closure is the implementation of a closure (function) implemented in Lua, including
   * references to its upvals and bytecode prototype.
   */
  struct lua_closure {
    bytecode_prototype *proto;
    small_vector<upval_view, 4> upval_views;
  };

  /**
   * lua_native_closure is the implementation of a closure (function) implemented in C++ (native).
   * It is simply a function reference, either a C function or C++ function / lambda.
   */
  struct lua_native_closure {
    using func_t = std::function<int(quokka_vm &)>;
    func_t func;
  };

  /**
   * A lua_table is the implementation of the Lua table datatype, allowing for a key-value store.
   * Note that in Quokka, we implement this as an array of pairs, to save on memory.
   * 
   * Table keys are based on equality. For bool, integer, number, and string, this is based on the
   * equality of the value. For objects (table, func), this is based on the instance of the value
   * (the object itself).
   */
  struct lua_table {
    struct node {
      lua_value key;
      lua_value value;

      node(const lua_value &k, const lua_value &v) : key(k), value(v) {}
    };
    small_vector<node, 8> entries;

    inline lua_value get(const char *str) {
      return get(lua_string{str});
    }

    /**
     * Get a value from the table by key.
     * 
     * @param k The key of the entry
     * @return The value of the entry
     */
    lua_value get(const lua_value &k) const;

    inline void set(const char *strk, const char *strv) { set(lua_string{strk}, lua_string{strv}); }
    inline void set(const lua_value &k, const char *strv) { set(k, lua_string{strv}); }
    inline void set(const char *strk, const lua_value &v) { set(lua_string{strk}, v); }

    /**
     * Set a value in the table by key.
     * 
     * @param k The key of the entry
     * @param v The value of the entry
     */
    void set(const lua_value &k, const lua_value &v);
  };

  using object_variant_t = std::variant<lua_nil, lua_table, lua_closure, lua_native_closure>;

  /**
   * Lua objects are datatypes that are described by more than just their value. Unlike
   * numbers, strings, and booleans, objects can be complex, such as tables. 
   * 
   * In Quokka LE, objects are allocated into one large pool (analogous to the heap),
   * and automatically dealloced when their usages reach zero. Note that objects are distinct
   * to upvalues, as objects do not (on their own) go above their own scope unless they are
   * used in an upvalue. 
   * 
   * A value may hold an object (or rather, a reference to an object), but an object is not a value.
   */
  struct lua_object : public refcount<object_variant_t> {
    using variant_t = object_variant_t;
    using refcount_t = refcount<variant_t>;
  };

  /**
   * The Upval is a construct of Lua that allows for values to exist oustide of their
   * regular scope. Consider the case of the anonymous function:
   * 
   * function createFunc()
   *  local i = 0
   *  local anon = function()
   *    i = i + 1
   *    return i
   *  end
   *  anon()
   *  return anon
   * end
   * 
   * In this case, "i" would usually go out of scope upon the return of createFunc(), 
   * but since it is being used by the anonymous function, we can't allow that to happen.
   * "i" is, in a sense, made global until all instances of the anonymous function are
   * not used anymore. "i" is an upval.
   * 
   * Note that "i", while still an upval, exists in two states at different times during 
   * execution. Before the local "i" goes out of scope (i.e. createFunc returns), the "i"
   * value is shared between createFunc and the anonymous function, hence it is on the stack.
   * In this state, "i" is referred to as an "open upval".
   * 
   * When createFunc returns, the stack entry for createFunc and its variables is popped. In 
   * this case, "i" would go out of scope and would no longer be accessible to the anonymous 
   * function. To solve this, each time a function is returned, its upvals (if still in use)
   * are moved to a global upval table, outside of the stack entirely. In this case, "i" is 
   * classified as a "closed upval" (it holds its own data, instead of pointing to a value
   * already on the stack).
   */
  struct lua_upval : public refcount<upval_variant_t> {
    using variant_t = upval_variant_t;
    using refcount_t = refcount<variant_t>;
  };

  /* Funcs */

  constexpr bool is_numeric(const lua_value &v) {
    return is<lua_integer>(v) || is<lua_number>(v);
  }

  bool tonumber(const lua_value &v, lua_number &out);
  bool tointeger(const lua_value &v, lua_integer &out);
  bool tostring(const lua_value &v, lua_string &out);

  inline lua_number tonumber(const lua_value &v) {
    lua_number n = 0;
    tonumber(v, n);
    return n;
  }

  inline lua_integer tointeger(const lua_value &v) {
    lua_integer i = 0;
    tointeger(v, i);
    return i;
  }

  inline lua_string tostring(const lua_value &v) {
    lua_string s("");
    tostring(v, s);
    return s;
  }

  inline object_view object(const lua_value &val) {
    return std::get<object_view>(val);
  }

  inline bool falsey(const lua_value &val) {
    return is<lua_nil>(val) || (is<bool>(val) && !std::get<bool>(val));
  }

  inline bool operator==(const lua_value &a, const lua_value &b) {
    if (a.index() == b.index() || (is_numeric(a) && is_numeric(b))) {
      return std::visit(overloaded {
        [&](auto &av) -> bool {
          return av == std::get<typename std::decay<decltype(av)>::type>(b);
        },
        [&](lua_integer av) -> bool {
          return av == (is<lua_integer>(b) ? std::get<lua_integer>(b) : std::get<lua_number>(b));
        },
        [&](lua_number av) -> bool {
          return av == (is<lua_integer>(b) ? std::get<lua_integer>(b) : std::get<lua_number>(b));
        }
      }, a);
    }
    return false;
  }

  inline bool operator!=(const lua_value &a, const lua_value &b) {
    return !(a == b);
  }

  inline bool operator<(const lua_value &a, const lua_value &b) {
    lua_number na, nb;
    if (a.index() == b.index() || (is_numeric(a) && is_numeric(b))) {
      if (tonumber(a, na) && tonumber(b, nb)) {
        return na < nb;
      } else if (is<lua_string>(a)) {
        return std::get<lua_string>(a) < std::get<lua_string>(b);
      }
    }
    return false;
  }

  inline bool operator<=(const lua_value &a, const lua_value &b) {
    lua_number na, nb;
    if (a.index() == b.index() || (is_numeric(a) && is_numeric(b))) {
      if (tonumber(a, na) && tonumber(b, nb)) {
        return na <= nb;
      } else if (is<lua_string>(a)) {
        return std::get<lua_string>(a) <= std::get<lua_string>(b);
      }
    }
    return false;
  }

  inline bool operator>(const lua_value &a, const lua_value &b) {
    return b < a;
  }

  inline bool operator>=(const lua_value &a, const lua_value &b) {
    return b <= a;
  }

  inline lua_table &table(object_view v) {
    return std::get<lua_table>(*v);
  }

  inline lua_table &table(lua_value &v) {
    return table(object(v));
  }

  inline lua_closure &lua_func(object_view v) {
    return std::get<lua_closure>(*v);
  }

  inline lua_closure &lua_func(lua_value &v) {
    return lua_func(object(v));
  }

  inline lua_native_closure &native_func(object_view v) {
    return std::get<lua_native_closure>(*v);
  }

  inline lua_native_closure &native_func(lua_value &v) {
    return native_func(object(v));
  }

  constexpr lua_tag_type get_tag_type(const lua_object &o) {
    return std::visit(overloaded {
      [&](lua_nil) -> lua_tag_type { return lua_tag_type::NIL; },
      [&](lua_table) -> lua_tag_type { return lua_tag_type::TABLE; },
      [&](lua_closure) -> lua_tag_type { return lua_tag_type::FUNC; },
      [&](lua_native_closure) -> lua_tag_type { return lua_tag_type::FUNC; }
    }, o.value());
  }

  constexpr lua_tag_type get_tag_type(const lua_value &v) {
    return std::visit(overloaded {
      [&](lua_nil) -> lua_tag_type { return lua_tag_type::NIL; },
      [&](bool) -> lua_tag_type { return lua_tag_type::BOOL; },
      [&](lua_number) -> lua_tag_type { return lua_tag_type::NUMBER; },
      [&](lua_integer) -> lua_tag_type { return lua_tag_type::NUMBER; },
      [&](lua_string) -> lua_tag_type { return lua_tag_type::STRING; },
      [&](void *) -> lua_tag_type { return lua_tag_type::USER_DATA; },
      [&](object_view) -> lua_tag_type { return lua_tag_type::OBJECT; }
    }, v);
  }
}  // namespace engine
}  // namespace quokka