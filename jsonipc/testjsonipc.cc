// Dedicated to the Public Domain under the Unlicense: https://unlicense.org/UNLICENSE
#include "jsonipc.hh"
#include <iostream>

#define MCHECK(MSG, ...) do { if (!strstr (MSG.c_str(), "\"error\":")) break; fprintf (stderr, "%s:%d: ERROR: %s\n", __FILE__, __LINE__, MSG.c_str()); return __VA_ARGS__; } while (0)

// == Testing ==
enum ErrorType {
  NONE,
  INVALID,
  FATAL
};

struct Copyable {
  int i = 111;
  float f = -0.05;
  std::string hello = "hello";
};

struct Base {
  Base() = default;
  Base(const Base&) = default;
  void need_copyablep (std::shared_ptr<Copyable> cp) { JSONIPC_ASSERT_RETURN (cp); }
  virtual ~Base() {}
};
using BaseP = std::shared_ptr<Base>;
struct Base2 {
  Base2() = default;
  Base2(const Base2&) = default;
  virtual ~Base2() {}
  Copyable randomize()  { Copyable c; c.i = rand(); c.f = rand() / 10.0; return c; }
};
struct Derived : Base, Base2, std::enable_shared_from_this<Derived> {
  const std::string name_;
  using Pair = std::pair<int, BaseP>;
  Derived (const std::string &name) : name_ (name) {}
  void   dummy0 ()                                { printf ("dummy0: NOP\n"); }
  bool   dummy1 (bool b)                          { printf ("dummy1: b=%s\n", b ? "true" : "false"); return b; }
  Pair   dummy2 (std::string s, int i)            { printf ("dummy2: s=\"%s\" i=%d\n", s.c_str(), i); return {}; }
  size_t dummy3 (Derived &d) const                { printf ("dummy3: Derived=%p this=%p\n", &d, this); return size_t (&d); }
  bool   dummy4 (float f, std::string s, long l)  { printf ("dummy4: this=%s %f '%s' %ld\n", name_.c_str(), f, s.c_str(), l); return 1; }
  void   dummy5 (const char *c, double d, Pair p) { printf ("dummy5: this=%s %d '%s' %f\n", name_.c_str(), p.first, c, d); }
  std::string dummy6 (int, const std::string &)   { return ""; }
  Derived* dummy7 () { return NULL; }
  Derived& dummy8 () { return *dummy7(); }
  Derived  dummy9 () { return dummy8(); }
  void defaults (bool a1 = true, ErrorType a2 = ErrorType::FATAL, std::string a3 = std::string ("a3"),
                 signed a4 = -4, float a5 = -0.5, const char *a6 = "a6", double a7 = 0.7,
                 size_t a8 = 8, Copyable *a9 = nullptr,
                 Base *b = nullptr) {} // see .set_d()
  template<class C> static void
  set_d (C &c) // see .defaults()
  {
    c.set_d ("defaults", &Derived::defaults, {
        true, ErrorType::FATAL, std::string ("a3"),
        signed (-4), float (-0.5), "a6", 1e-70,
        size_t (8), nullptr,
        {} // not possible: (Base*) nullptr,
      });
  }
};

static size_t
json_objectid (const Jsonipc::JsonValue &value)
{
  if (value.IsObject())
    {
      auto it = value.FindMember ("$id");
      if (it != value.MemberEnd())
        return Jsonipc::from_json<size_t> (it->value);
    }
  return 0;
}

template<typename R> R
parse_result (size_t id, const std::string json_reply)
{
  rapidjson::Document document;
  document.Parse<Jsonipc::rapidjson_parse_flags> (json_reply.data(), json_reply.size());
  if (!document.HasParseError())
    {
      size_t id_ = 0;
      const Jsonipc::JsonValue *result = NULL;
      for (const auto &m : document.GetObject())
        if (m.name == "id")
          id_ = Jsonipc::from_json<size_t> (m.value, 0);
        else if (m.name == "result")
          result = &m.value;
      if (id_ == id && result)
        return Jsonipc::from_json<R> (*result);
    }
  return R();
}

static void
test_jsonipc (bool dispatcher_shell, bool printer)
{
  using namespace Jsonipc;
  rapidjson::Document doc;
  auto &a = doc.GetAllocator();

  // test basics
  JSONIPC_ASSERT_RETURN (false == from_json<bool> (JsonValue()));
  JSONIPC_ASSERT_RETURN (true == from_json<bool> (JsonValue (true)));
  JSONIPC_ASSERT_RETURN (true == from_json<bool> (JsonValue(), true));
  JSONIPC_ASSERT_RETURN (false == from_json<bool> (JsonValue(), false));
  JSONIPC_ASSERT_RETURN (from_json<bool> (to_json (true, a)) == true);
  JSONIPC_ASSERT_RETURN (from_json<bool> (to_json (false, a)) == false);
  JSONIPC_ASSERT_RETURN (from_json<size_t> (to_json (1337, a)) == 1337);
  JSONIPC_ASSERT_RETURN (from_json<ssize_t> (to_json (-1337, a)) == -1337);
  JSONIPC_ASSERT_RETURN (from_json<float> (to_json (-0.5, a)) == -0.5);
  JSONIPC_ASSERT_RETURN (from_json<double> (to_json (1e20, a)) == 1e20);
  JSONIPC_ASSERT_RETURN (from_json<const char*> (to_json ("Om", a)) == std::string ("Om"));
  JSONIPC_ASSERT_RETURN (from_json<std::string> (to_json (std::string ("Ah"), a)) == "Ah");
  JSONIPC_ASSERT_RETURN (strcmp ("HUM", from_json<const char*> (to_json ((const char*) "HUM", a))) == 0);

  // register test classes and methods
  Jsonipc::Enum<ErrorType> enum_ErrorType;
  enum_ErrorType
    .set (ErrorType::NONE, "NONE")
    .set (ErrorType::INVALID, "INVALID")
    .set (ErrorType::FATAL, "FATAL")
    ;

  Jsonipc::Class<Base> class_Base;
  class_Base
    .set ("need_copyablep", &Base::need_copyablep)
    ;
  Jsonipc::Class<Base2> class_Base2;
  Jsonipc::Serializable<Copyable> class_Copyable;
  class_Copyable
    // .copy ([] (const Copyable &o) { return std::make_shared<std::decay<decltype (o)>::type> (o); })
    .set ("i", &Copyable::i)
    .set ("f", &Copyable::f)
    .set ("hello", &Copyable::hello)
    ;
  Jsonipc::Class<Derived> class_Derived;
  class_Derived
    .inherit<Base>()
    .inherit<Base2>()
    .set ("dummy0", &Derived::dummy0)
    .set ("dummy1", &Derived::dummy1)
    .set ("dummy2", &Derived::dummy2)
    .set ("dummy3", &Derived::dummy3)
    .set ("dummy4", &Derived::dummy4)
    .set ("dummy5", &Derived::dummy5)
    .set ("dummy6", &Derived::dummy6)
    .set ("dummy7", &Derived::dummy7)
    .set ("dummy8", &Derived::dummy8)
    .set ("dummy9", &Derived::dummy9)
    .set ("randomize", &Derived::randomize)
    ;
  Derived::set_d (class_Derived);

  // Provide scope and instance ownership during dispatch_message()
  InstanceMap imap;
  Scope temporary_scope (imap); // needed by to_/from_json and dispatcher

  // test bindings
  auto objap = std::make_shared<Derived> ("obja");
  Derived &obja = *objap;
  JSONIPC_ASSERT_RETURN (to_json (obja, a) == to_json (obja, a));
  JSONIPC_ASSERT_RETURN (&obja == from_json<Derived*> (to_json (obja, a)));
  JSONIPC_ASSERT_RETURN (ptrdiff_t (&static_cast<Base&> (obja)) == ptrdiff_t (&obja));
  JSONIPC_ASSERT_RETURN (ptrdiff_t (&static_cast<Base2&> (obja)) > ptrdiff_t (&obja));
  // given the same id, Base and Base2 need to unwrap to different addresses due to multiple inheritance
  JSONIPC_ASSERT_RETURN (&obja == from_json<Base*> (to_json (obja, a)));
  JSONIPC_ASSERT_RETURN (from_json<Base2*> (to_json (obja, a)) == &static_cast<Base2&> (obja));
  auto objbp = std::make_shared<Derived> ("objb");
  Derived &objb = *objbp;
  JSONIPC_ASSERT_RETURN (&obja != &objb);
  // const size_t idb = class_Derived.wrap_object (objb);
  JSONIPC_ASSERT_RETURN (from_json<std::shared_ptr<Derived>> (to_json (objb, a)).get() == &objb);
  JSONIPC_ASSERT_RETURN (ptrdiff_t (&static_cast<Base&> (objb)) == ptrdiff_t (&objb));
  JSONIPC_ASSERT_RETURN (ptrdiff_t (&static_cast<Base2&> (objb)) > ptrdiff_t (&objb));
  JSONIPC_ASSERT_RETURN (from_json<std::shared_ptr<Base>> (to_json (objb, a)).get() == &static_cast<Base&> (objb));
  JSONIPC_ASSERT_RETURN (from_json<std::shared_ptr<Base2>> (to_json (objb, a)).get() == &static_cast<Base2&> (objb));
  JSONIPC_ASSERT_RETURN (to_json (obja, a) != to_json (objb, a));
  auto objcp = std::make_shared<Derived> ("objc");
  Derived &objc = *objcp;
  JSONIPC_ASSERT_RETURN (&objc == &from_json<Derived&> (to_json (objc, a)));
  JSONIPC_ASSERT_RETURN (&objc == &from_json<Base&> (to_json (objc, a)));
  JSONIPC_ASSERT_RETURN (&objc == &from_json<Base2&> (to_json (objc, a)));
  JSONIPC_ASSERT_RETURN (to_json (objb, a) != to_json (objc, a));
  JSONIPC_ASSERT_RETURN (to_json (objc, a) == to_json (objc, a));
  const JsonValue jva = to_json (obja, a);
  const JsonValue jvb = to_json (objb, a);
  const JsonValue jvc = to_json (objc, a);
  JSONIPC_ASSERT_RETURN (from_json<Derived*> (jva) == &obja);
  JSONIPC_ASSERT_RETURN (&from_json<Derived> (jvb) == &objb);
  JSONIPC_ASSERT_RETURN (&from_json<Derived&> (jvc) == &objc);

  // Serializable tests
  std::string result;
  Copyable c1 { 2345, -0.5, "ehlo" };
  JsonValue jvc1 = to_json (c1, a);
  Copyable c2 = from_json<Copyable&> (jvc1);
  JSONIPC_ASSERT_RETURN (c1.i == c2.i && c1.f == c2.f && c1.hello == c2.hello);

  // dispatcher tests
  IpcDispatcher dispatcher;
  auto d1p = std::make_shared<Derived> ("dood");
  Derived &d1 = *d1p;
  JsonValue jvd1 = to_json (d1, a);
  const size_t d1id = json_objectid (jvd1);
  JSONIPC_ASSERT_RETURN (d1id == 4); // used in the next few lines
  result = dispatcher.dispatch_message (R"( {"id":123,"method":"randomize","params":[{"$id":4}]} )");
  MCHECK (result);
  const Copyable c0;
  const Copyable *c3 = parse_result<Copyable*> (123, result);
  JSONIPC_ASSERT_RETURN (c3 && (c3->i != c0.i || c3->f != c0.f));
  result = dispatcher.dispatch_message (R"( {"id":123,"method":"need_copyablep","params":[{"$id":4},{}]} )");
  MCHECK (result);
  result = dispatcher.dispatch_message (R"( {"id":444,"method":"randomize","params":[{"$id":4}]} )");
  MCHECK (result);
  const Copyable *c4 = parse_result<Copyable*> (444, result);
  JSONIPC_ASSERT_RETURN (c4 && (c4->i != c3->i || c4->f != c3->f));
  result = dispatcher.dispatch_message (R"( {"id":111,"method":"randomize","params":[{"$id":4}]} )");
  MCHECK (result);
  const Copyable *c5 = parse_result<Copyable*> (111, result);
  JSONIPC_ASSERT_RETURN (c5 && (c5->i != c4->i || c5->f != c4->f));

  if (printer)
    {
      printf ("%s\n", Jsonipc::ClassPrinter::to_string().c_str());
    }

  // CLI test server
  if (dispatcher_shell)
    {
      for (std::string line; std::getline (std::cin, line); )
        std::cout << dispatcher.dispatch_message (line) << std::endl;
      // Feed example lines:
      // {"id":123,"method":"dummy3","params":[2],"this":1}
    }

  // unregister thisids for objects living on the stack
  forget_json_id (json_objectid (jva));
  JSONIPC_ASSERT_RETURN (from_json<Derived*> (jva) == nullptr);
  forget_json_id (json_objectid (jvb));
  JSONIPC_ASSERT_RETURN (from_json<Derived*> (jvb) == (Derived*) nullptr);
  forget_json_id (json_objectid (jvc));
  JSONIPC_ASSERT_RETURN (from_json<Derived*> (jvc) == (Derived*) nullptr);

  printf ("  OK       %s\n", __func__);
}

#ifdef STANDALONE
int
main (int argc, char *argv[])
{
  const bool dispatcher_shell = argc > 1 && 0 == strcmp (argv[1], "--shell");
  const bool printer = argc > 1 && 0 == strcmp (argv[1], "--print");
  test_jsonipc (dispatcher_shell, printer);
  return 0;
}
#endif
