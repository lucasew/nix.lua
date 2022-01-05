extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

#include <nix/eval.hh> // eval stuff
#include <nix/value.hh> // basic types
#include <nix/globals.hh> // initPlugins
#include <nix/store-api.hh> // openStore
#include <nix/machines.hh>

static const struct luaL_Reg libnix [] = {
    {NULL, NULL}
};

// https://gist.github.com/jzrake/2408691

#define STACKDUMP {                                                     \
    for (int i=1; i<=lua_gettop(L); ++i) {                              \
      printf("%d: %s %s\n",i,luaL_typename(L,i),lua_tostring(L,i));     \
    }                                                                   \
  }                                                                     \
// ---------------------------------------------------------------------------
#define RETURN_ATTR_OR_CALL_SUPER(S) {                                  \
    AttributeMap::iterator m = attr.find(method_name);                  \
    return m == attr.end() ? S::__getattr__(method_name) : m->second;   \
  }    

class LuaCppObject
{
 protected:
  typedef int (*LuaInstanceMethod)(lua_State *L);
  typedef std::map<std::string, LuaInstanceMethod> AttributeMap;

  // Used as the key into the symbol table (with weak values) of registered
  // objects at global key `LuaCppObject`.
  int __refid;

 public:

  // =======================================
  // P U B L I C   C L A S S   M E T H O D S
  // =======================================

  LuaCppObject() : __refid(LUA_NOREF) { }
  virtual ~LuaCppObject() { }

  static void Init(lua_State *L)
  // ---------------------------------------------------------------------------
  // Set up the global table for LuaCppObject to have weak values, so that its
  // entries are garbage collected. Call this once for each Lua instance.
  // ---------------------------------------------------------------------------
  {
    lua_newtable(L);
    lua_newtable(L);
    lua_pushstring(L, "v");
    lua_setfield(L, -2, "__mode");
    lua_setmetatable(L, -2);
    lua_setglobal(L, "LuaCppObject");
  }
  template <class T> static void Register(lua_State *L, int pos)
  // ---------------------------------------------------------------------------
  // Registers the constructor for a given class in the table at position `pos`.
  // ---------------------------------------------------------------------------
  {
    pos = lua_absindex(L, pos);
    lua_pushcfunction(L, T::new_lua_obj);
    lua_setfield(L, pos, demangle(typeid(T).name()).c_str());
  }

protected:

  // =================================
  // U T I L I T Y   F U N C T I O N S
  // =================================

  template <class T> static T *checkarg(lua_State *L, int pos)
  // ---------------------------------------------------------------------------
  // This function first ensures that the argument at position `pos` is a valid
  // user data. If so, it tries to dynamic_cast it to the template parameter
  // `T`. This cast will fail if the object does not inherit from `T`, causing a
  // graceful Lua error.
  // ---------------------------------------------------------------------------
  {
    void *object_p = lua_touserdata(L, pos);
    if (object_p == NULL) {
      luaL_error(L, "invalid type");
    }

    LuaCppObject *cpp_object = *static_cast<LuaCppObject**>(object_p);
    T *result = dynamic_cast<T*>(cpp_object);

    if (result == NULL) {
      luaL_error(L, "object of type '%s' is not a subtype of '%s'",
                 cpp_object->get_type().c_str(),
                 demangle(typeid(T).name()).c_str());
    }
    return result;
  }
  static void push_lua_obj(lua_State *L, LuaCppObject *object)
  {
    lua_getglobal(L, "LuaCppObject");
    lua_rawgeti(L, -1, object->__refid);
    lua_remove(L, -2);
  }
  static int make_lua_obj(lua_State *L, LuaCppObject *object)
  {
    LuaCppObject **place = (LuaCppObject**)
      lua_newuserdata(L, sizeof(LuaCppObject*));
    *place = object;

    lua_newtable(L);

    lua_pushcfunction(L, LuaCppObject::__index);
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, LuaCppObject::__tostring);
    lua_setfield(L, -2, "__tostring");
    lua_pushcfunction(L, LuaCppObject::__gc);
    lua_setfield(L, -2, "__gc");

    lua_setmetatable(L, -2);

    // Register the object with a unique reference id for easy retrieval as a
    // Lua object.
    // -------------------------------------------------------------------------
    lua_getglobal(L, "LuaCppObject");
    lua_pushvalue(L, -2);
    object->__refid = luaL_ref(L, -2);
    lua_pop(L, 1);

    return 1;
  }

#ifdef __GNUC__
  // ---------------------------------------------------------------------------
  // Demangling names on gcc works like this:
  // ---------------------------------------------------------------------------
  static std::string demangle(const char *mname)
  {
    static int status;
    char *realname = abi::__cxa_demangle(mname, 0, 0, &status);
    std::string ret = realname;
    free(realname);
    return ret;
  }
#else
  static std::string demangle(const char *mname)
  {
    return std::string(mname);
  }
#endif // __GNUC__


 protected:
  virtual std::string get_type()
  // ---------------------------------------------------------------------------
  // May be over-ridden by derived classes in case a different type name is
  // desired. This function is called by the default `tostring` metamethod.
  // ---------------------------------------------------------------------------
  {
    return demangle(typeid(*this).name());
  }
  virtual std::string tostring()
  {
    std::stringstream ss;
    ss<<"<"<<this->get_type()<<" instance at "<<this<<">";
    return ss.str();
  }


  // ===============================
  // I N S T A N C E   M E T H O D S
  // ===============================

  virtual LuaInstanceMethod __getattr__(std::string &method_name)
  // ---------------------------------------------------------------------------
  // The attributes below are inherited by all LuaCppObject's. If an attribute
  // does not belong to a particular class instance, the super is invoked until
  // we reach this function, at which point NULL is returned.
  // ---------------------------------------------------------------------------
  {
    AttributeMap attr;
    attr["get_refid"] = _get_refid_;
    attr["get_type"] = _get_type_;
    AttributeMap::iterator m = attr.find(method_name);
    return m == attr.end() ? NULL : m->second;
  }
  static int _get_refid_(lua_State *L)
  {
    LuaCppObject *self = checkarg<LuaCppObject>(L, 1);
    lua_pushnumber(L, self->__refid);
    return 1;
  }
  static int _get_type_(lua_State *L)
  {
    LuaCppObject *self = checkarg<LuaCppObject>(L, 1);
    lua_pushstring(L, self->get_type().c_str());
    return 1;
  }

  // =====================
  // M E T A M E T H O D S
  // =====================

  static int __index(lua_State *L)
  // ---------------------------------------------------------------------------
  // Arguments:
  //
  // (1) object: a user data pointing to a LuaCppObject
  // (2) method_name: a string
  //
  // Returns: a static c-function which wraps the instance method
  //
  // ---------------------------------------------------------------------------
  {
    LuaCppObject *object = *static_cast<LuaCppObject**>(lua_touserdata(L, 1));
    std::string method_name = lua_tostring(L, 2);

    LuaInstanceMethod m = object->__getattr__(method_name);

    if (m == NULL) {
      luaL_error(L, "'%s' has no attribute '%s'", object->get_type().c_str(),
                 method_name.c_str());
    }

    lua_pushcfunction(L, m);
    return 1;
  }

  static int __gc(lua_State *L)
  // ---------------------------------------------------------------------------
  // Arguments:
  //
  // (1) object: a user data pointing to a LuaCppObject
  //
  // Returns: nothing
  // ---------------------------------------------------------------------------
  {
    LuaCppObject *object = *static_cast<LuaCppObject**>(lua_touserdata(L, 1));

    // Unregister the object
    // -------------------------------------------------------------------------
    lua_getglobal(L, "LuaCppObject");
    luaL_unref(L, -1, object->__refid);
    lua_pop(L, 1);

    //    printf("killing object with refid %d...\n", object->__refid);

    delete object;
    return 0;
  }
  static int __tostring(lua_State *L)
  {
    LuaCppObject *object = *((LuaCppObject**) lua_touserdata(L, 1));
    lua_pushstring(L, object->tostring().c_str());
    return 1;
  }
} ;


class NixStore : public LuaCppObject {
    private:
        nix::ref<nix::Store> store;
    public:
        NixStore() : store(nix::openStore()) {}
        virtual ~NixStore() {}
    protected:
        virtual LuaInstanceMethod __getattr__(std::string &method_name) {
        }
        static int getUri(lua_State *L) {
            nix::ref<nix::Store>> store = checkarg<nix::ref<nix::Store>>(L, 1);
            if (store.getUri) {
                auto uri = store.getUri();
                lua_pushstring(uri);
                return 1;
            } else {
                lua_pushnil(L);
                return 1;
            }
        }
};

int initialized = 0;
void init() {
    if (initialized) {
        return;
    }
    nix::initPlugins();
    nix::initGC();
    initialized = 1;
}

static void register_NixStore(lua_State* L) {
    const char* objName = "NixStore";
    lua_register(L, objName, luaOpenStore);
    luaL_newmetatable(L, objName);
}

int luaopen_libnix(lua_State *L) {
    init();
    luaL_newlib(L, libnix);
    return 1;
}
