#include "nildata.hpp"

#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
int checks = 0;
int failures = 0;

void expect(bool condition, const std::string &name) {
  ++checks;
  if (!condition) { ++failures; std::cerr << "FAIL: " << name << '\n'; }
}

void rejects(const std::function<void()> &action, const std::string &name) {
  try { action(); expect(false, name); }
  catch (const std::exception &) { expect(true, name); }
}

std::string succeeds(const std::string &operation, const std::string &source = {},
                     const std::string &argument = {}, std::int64_t index = 0) {
  const auto result = nil::data::request(operation, source, argument, index);
  expect(result.size() == 2 && result[0].empty() && result[1].find('\0') == std::string::npos,
         "request success: " + operation + (result.size() == 2 ? " " + result[0] : " invalid response"));
  return result.size() == 2 ? result[1] : "";
}

void fails(const std::string &operation, const std::string &source = {},
           const std::string &argument = {}, std::int64_t index = 0) {
  const auto result = nil::data::request(operation, source, argument, index);
  expect(result.size() == 2 && !result[0].empty() && result[1].empty() &&
         result[0].find('\0') == std::string::npos, "request rejection: " + operation);
}

std::string nested(unsigned depth) {
  return std::string(depth, '[') + "null" + std::string(depth, ']');
}

std::string wide(std::size_t values) {
  std::string result = "[";
  for (std::size_t i = 0; i < values; ++i) { if (i) result += ','; result += "null"; }
  return result + ']';
}

void conversions() {
  const std::string document =
      "# A data document, not executable Matrix.\n"
      "nildata 1 # version\n"
      "object\n"
      "  enabled is true\n"
      "  missing is null\n"
      "  workers is 99999999999999999999999999999999999999999\n"
      "  scale is -1.60e+9999\n"
      "  name is \"  # end object \\\" \\n\\t\\u0000\" # comment\n"
      "  \"\" is an array\n"
      "    item is an object\n"
      "      end is false\n"
      "    end object\n"
      "    item is -0\n"
      "  end array\n"
      "end object\n";
  const auto json = nil::data::to_json(document);
  const auto normalized = nil::data::normalize(document);
  expect(normalized.find("nildata 1\nobject\n") == 0, "canonical root spelling");
  expect(normalized.find("# comment") == std::string::npos, "comments omitted");
  expect(normalized.find("end is false") != std::string::npos, "keyword member remains valid");
  expect(nil::data::to_json(normalized) == json, "NILData normalization preserves values");
  expect(nil::data::from_json(json) == normalized, "JSON shares deterministic writer");
  expect(nil::data::normalize(normalized) == normalized, "normalization idempotent");
  expect(json.find("-1.60e+9999") != std::string::npos, "decimal lexeme not converted to float");
  expect(json.find("99999999999999999999999999999999999999999") != std::string::npos,
         "integer lexeme not narrowed");
  expect(json.find("\\u0000") != std::string::npos && json.find('\0') == std::string::npos,
         "escaped NUL preserved as JSON");
  expect(nil::data::to_json("nildata 1\r\nan array\r\nitem is 1\r\nend array\r\n") == "[1]",
         "root article and CRLF");
  expect(nil::data::to_json("nildata 1\nobject\n\"quote\\\"#key\" is \"#value\" # comment\nend object") ==
         "{\"quote\\\"#key\":\"#value\"}", "comment detection respects escaped quotes");
  expect(nil::data::from_json("{\"z\":1,\"a\":2}") ==
         "nildata 1\nobject\n  a is 2\n  z is 1\nend object\n", "sorted keys");
  for (const auto &source : std::vector<std::string>{"null", "true", "false", "0", "-0", "1.00", "1E-2",
           "\"\"", "\"  leading and trailing  \"", "\"\\ud83d\\ude80\"", "{}", "[]",
           "{\"line\\nkey\":[\"end object\",{},[],false]}", "\"caf\xc3\xa9\""}) {
    const auto data = nil::data::from_json(source);
    expect(nil::data::to_json(data) == succeeds("to_json", source), "JSON value round trip");
  }
}

void malformed() {
  for (const auto &source : std::vector<std::string>{
      "", "object\nend object\n", "nildata 2\nnull\n", "nildata 1\n",
      "nildata 1\nnull\nfalse\n", "nildata 1\nobject\n", "nildata 1\narray\nend object\n",
      "nildata 1\nobject\na is 1\na is 2\nend object\n",
      "nildata 1\nobject\na is 1\n\"\\u0061\" is 2\nend object\n",
      "nildata 1\nobject\nbad-name is 1\nend object\n",
      "nildata 1\nobject\n\"name\"is 1\nend object\n",
      "nildata 1\nobject\na becomes 1\nend object\n",
      "nildata 1\narray\nname is 1\nend array\n",
      "nildata 1\narray\n\"item\" is 1\nend array\n",
      "nildata 1\nobject\na is object\nend object\nend object\n",
      "nildata 1\n[]\n", "nildata 1\n{}\n", "nildata 1\nlist\nend list\n",
      "nildata 1\n\"unterminated\n", "nildata 1\n\"bad\\q\"\n",
      "nildata 1\n\"\\ud800\"\n", "nildata 1\n\"\\udc00\"\n",
      "nildata 1\n01\n", "nildata 1\n+1\n", "nildata 1\n-.5\n",
      "nildata 1\n1.\n", "nildata 1\n1e\n", "nildata 1\nNaN\n",
      "nildata 1\nnegative 1.6\n", "nildata 1\ntrue false\n"}) {
    rejects([&] { (void)nil::data::to_json(source); }, "invalid NILData rejected");
    fails("load", source);
  }
  for (const auto &source : std::vector<std::string>{"", "{", "[1,]", "{\"x\":1,\"x\":2}",
      "{\"a\":1,\"\\u0061\":2}", "{\"x\":}", "\"\\u000x\"", "01", "true false", "1e+", "--1"}) {
    rejects([&] { (void)nil::data::from_json(source); }, "invalid JSON rejected");
    fails("from_json", source);
  }
  const std::string bad_utf8("\xc0\xaf", 2);
  fails("string", "", bad_utf8);
  fails("load", "nildata 1\nnull\n# " + bad_utf8);
  fails("from_json", "\"" + bad_utf8 + "\"");
  for (const auto &operation : {"kind", "text", "number", "boolean", "size", "get", "has", "at",
                                "key", "set", "append", "replace", "erase_key", "erase_at", "dump", "to_json"})
    fails(operation, "{invalid public backing", "x");
  fails("string", "", std::string("a\0b", 3));
  fails("has", "{}", std::string("a\0b", 3));
  fails("load", std::string("nildata 1\nnull\n#\0", 18));
  fails("unsupported", "null");
  try { (void)nil::data::to_json("nildata 1\nobject\n  x is 01\nend object\n"); }
  catch (const std::exception &error) { expect(std::string(error.what()).find("line 3") != std::string::npos, "line diagnostics"); }
}

void values() {
  expect(succeeds("null") == "null", "null constructor");
  expect(succeeds("object") == "{}", "object constructor");
  expect(succeeds("array") == "[]", "array constructor");
  expect(succeeds("string", "", "a\nb") == "\"a\\u000ab\"", "string constructor escapes controls");
  expect(succeeds("integer", "", "999999999999999999999999999") == "999999999999999999999999999", "exact integer constructor");
  expect(succeeds("decimal", "", "1e9999") == "1e9999", "exact decimal constructor");
  expect(succeeds("flag", "", "false") == "false", "boolean constructor");
  fails("integer", "", "1.0"); fails("integer", "", "1e2"); fails("integer", "", " 1");
  fails("decimal", "", "1"); fails("decimal", "", "\"1.0\""); fails("flag", "", "1"); fails("flag", "", " true");
  const std::vector<std::pair<std::string, std::string>> kinds = {
      {"null", "null"}, {"true", "boolean"}, {"-1", "integer"}, {"1e0", "decimal"},
      {"\"text\"", "text"}, {"[]", "array"}, {"{}", "object"}};
  for (const auto &entry : kinds) expect(succeeds("kind", entry.first) == entry.second, "kind classification");
  expect(succeeds("text", "\"a\\nb\"") == "a\nb", "decoded text");
  expect(succeeds("text", "\"\"").empty(), "empty text extraction");
  expect(succeeds("number", "-0.00E+12") == "-0.00E+12", "number extraction preserves lexeme");
  expect(succeeds("boolean", "false") == "false", "boolean extraction");
  fails("text", "null"); fails("text", "\"\\u0000\""); fails("number", "\"1\""); fails("boolean", "0");
  expect(succeeds("get", "{\"present\":null}", "present") == "null", "null member is present");
  expect(succeeds("has", "{\"present\":null}", "present") == "true", "has present null");
  expect(succeeds("has", "{\"present\":null}", "absent") == "false", "has absent");
  fails("get", "{}", "absent"); fails("has", "[]", "absent");
  expect(succeeds("key", "{\"\":1,\"z\":2}", "", 0).empty(), "empty key extraction");
  expect(succeeds("key", "{\"z\":1,\"a\":2}", "", 1) == "z", "sorted key traversal");
  fails("key", "{\"\\u0000\":1}"); fails("key", "{}", "", 0); fails("key", "{}", "", -1);
  expect(succeeds("at", "[null,\"b\"]", "", 1) == "\"b\"", "array lookup");
  fails("at", "[]", "", 0); fails("at", "[1]", "", -1); fails("at", "{}", "", 0);
  expect(succeeds("size", "[]") == "0", "empty array size");
  expect(succeeds("size", "{\"a\":null}") == "1", "object size"); fails("size", "null");
  expect(succeeds("set", "{}", "[\"key\",[1,true]]") == "{\"key\":[1,true]}", "object set");
  expect(succeeds("set", "{\"key\":1}", "[\"key\",null]") == "{\"key\":null}", "object replace");
  expect(succeeds("set", "{}", "[\"\\u0000\",\"\\u0000\"]") == "{\"\\u0000\":\"\\u0000\"}", "escaped NUL stays encoded");
  fails("set", "{}", "[]"); fails("set", "{}", "[1,2]"); fails("set", "[]", "[\"a\",1]");
  expect(succeeds("append", "[1]", "{\"a\":false}") == "[1,{\"a\":false}]", "array append");
  expect(succeeds("replace", "[1,2]", "null", 1) == "[1,null]", "array replacement");
  fails("replace", "[]", "null", 0); fails("replace", "[1]", "null", -1);
  expect(succeeds("erase_at", "[1,2,3]", "", 1) == "[1,3]", "array remove preserves order");
  fails("erase_at", "[]"); fails("erase_at", "[1]", "", -1);
  expect(succeeds("erase_key", "{\"a\":1,\"b\":2}", "a") == "{\"b\":2}", "object remove");
  expect(succeeds("erase_key", "{}", "missing") == "{}", "absent object removal");
}

void legacy() {
  const std::string source = "object root\nfield truth\ntrue\nend truth\nlist names\nitem\nhello\nend item\nend names\nend root\n";
  expect(nil::data::to_json(nil::data::import_legacy(source)) == "{\"names\":[\"hello\"],\"truth\":\"true\"}",
         "legacy library keeps scalar text and explicit list");
  expect(succeeds("legacy", source) == "{\"names\":[\"hello\"],\"truth\":\"true\"}", "legacy runtime result is JSON");
  const std::string compiler = "field enabled\ntrue\nend enabled\nobject values\nfield 0\n42\nend 0\nend values\n";
  expect(nil::data::to_json(nil::data::import_legacy(compiler, true)) ==
         "{\"enabled\":\"true\",\"values\":{\"0\":\"42\"}}", "compiler legacy keeps names and ambiguous types");
  expect(succeeds("legacy_compiler", "field name\nx\nend name\n") == "{\"name\":\"x\"}", "single compiler member remains named");
  expect(succeeds("import_legacy_compiler", "") == "{}", "empty compiler object");
  expect(nil::data::to_json(nil::data::import_legacy("field root\nfirst\nsecond\nend root\n")) ==
         "\"first\\u000asecond\"", "legacy multiline text");
  for (const auto &bad : std::vector<std::string>{"", "object root\n", "field root\ntext\n",
       "list root\nitem\ntext\nend root\n", "object root\nunknown\nend root\n",
       "object root\nfield a\nx\nend a\nfield a\ny\nend a\nend root\n",
       "field a\nx\nend a\nfield b\ny\nend b\n"})
    rejects([&] { (void)nil::data::import_legacy(bad); }, "malformed legacy rejected");
  fails("legacy_compiler", "list values\nend values\n");
}

void limits() {
  const auto depth32 = nested(32);
  expect(nil::data::to_json(nil::data::from_json(depth32)) == depth32, "depth 32 boundary");
  fails("from_json", nested(33));
  fails("append", "[]", depth32);
  fails("replace", "[null]", depth32, 0);
  expect(!succeeds("set", "{}", "[\"nested\"," + nested(31) + "]").empty(), "set depth boundary");
  std::string excessive_data = "nildata 1\narray\n";
  for (int i = 0; i < 32; ++i) excessive_data += "item is an array\n";
  excessive_data += "item is null\n";
  for (int i = 0; i < 33; ++i) excessive_data += "end array\n";
  fails("load", excessive_data);
  const auto node_limit = wide(nil::data::max_nodes - 1);
  expect(succeeds("size", node_limit) == "65535", "node limit accepted");
  expect(nil::data::to_json(nil::data::from_json(node_limit)) == node_limit, "node limit round trip");
  fails("from_json", wide(nil::data::max_nodes));
  fails("append", node_limit, "null");
  fails("replace", node_limit, "[null]", 0);
  std::string object = "{";
  for (std::size_t i = 0; i < nil::data::max_nodes - 2; ++i) {
    if (i) object += ',';
    object += '"' + std::to_string(i) + "\":null";
  }
  object += '}';
  fails("set", object, "[\"extra\",[null]]");
  fails("load", std::string(nil::data::max_bytes + 1, ' '));
  fails("from_json", std::string(nil::data::max_bytes + 1, ' '));
  fails("string", "", std::string(nil::data::max_bytes + 1, 'a'));
  fails("string", "", std::string(nil::data::max_bytes / 5, '\t'));
  const auto long_string = '"' + std::string(nil::data::max_bytes - 2, 'a') + '"';
  expect(succeeds("kind", long_string) == "text", "input byte limit accepted");
  fails("dump", long_string);
  fails("append", std::string("[\"") + std::string(nil::data::max_bytes - 4, 'a') + "\"]", "null");
}
} // namespace

int main() {
  try { conversions(); malformed(); values(); legacy(); limits(); }
  catch (const std::exception &error) { expect(false, std::string("unexpected exception: ") + error.what()); }
  if (failures) std::cerr << failures << " of " << checks << " checks failed\n";
  else std::cout << checks << " NILData engine checks passed\n";
  return failures ? 1 : 0;
}
