#include "parser.hpp"
#include "type_checker.hpp"
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {
int failures = 0;
std::string program(const std::string &body, const std::string &extra = "") {
  return extra + "\nfn main() -> i32\n" + body + "\nreturn 0\nend fn\n";
}
void check(const std::string &name, const std::string &source, bool expected) {
  ny::parser parser(source);
  auto tree = parser.parse_program();
  if (!tree || parser.has_errors()) {
    ++failures;
    std::cerr << "FAIL fixture parse " << name << '\n';
    for (const auto &diagnostic : parser.diagnostics())
      std::cerr << diagnostic.line << ": " << diagnostic.message << '\n';
    return;
  }
  ny::type_checker checker;
  const bool actual = checker.check(*tree);
  if (actual != expected) {
    ++failures;
    std::cerr << "FAIL " << name << ": expected " << expected << ", got " << actual << '\n';
    for (const auto &diagnostic : checker.get_diagnostics())
      std::cerr << diagnostic.line << ": " << diagnostic.message << '\n';
  }
  // Reusing the checker/tree must recompute, never retain a stale permission.
  if (checker.check(*tree) != actual) {
    ++failures;
    std::cerr << "FAIL unstable repeated check " << name << '\n';
  }
}
}
int main() {
  const std::string owner = "let mut buffer = owned<i32>(4)\n";
  const std::vector<std::pair<std::string, std::string>> good = {
    {"owner_read_write", program(owner + "buffer[0] = 7\n*buffer = 8\nprintln(buffer[0])\nprintln(*buffer)")},
    {"move", program(owner + "let mut moved = owned_move(buffer)\n*moved = 4\nprintln(*moved)")},
    {"shared_borrows", program(owner + "let a = owned_borrow(buffer)\nlet b = owned_borrow(buffer)\nprintln(*a + *b)\nprintln(*buffer)")},
    {"mutable_borrow", program(owner + "if true then\nlet a = owned_borrow_mut(buffer)\na[0] = 9\nprintln(*a)\nend if\nprintln(*buffer)")},
    {"borrow_scope_then_move", program(owner + "if true then\nlet a = owned_borrow(buffer)\nprintln(*a)\nend if\nlet moved = owned_move(buffer)\nprintln(*moved)")},
    {"ordinary_shadow", program(owner + "if true then\nlet buffer = 9\nprintln(buffer)\nend if\nprintln(*buffer)")},
    {"loop_local", program("for index in range(3)\n" + owner + "*buffer = index\nend for")},
    {"branch_moves", program(owner + "if true then\nlet left = owned_move(buffer)\nprintln(*left)\nelse\nlet right = owned_move(buffer)\nprintln(*right)\nend if")},
    {"alias", program("let mut buffer: Words = owned<i32>(3)\nbuffer[0] = 5", "type Words = owned<i32>\n")},
    {"fault_local", program("attempt\n" + owner + "*buffer = 7\nraise Problem { code: 1 }\nfault Problem as issue\nprintln(issue.code)\nend attempt", "struct Problem\ncode:i32\nend struct\n")},
    {"parallel", program("let input: array<i32> = [1, 2]\nlet output = parallel_map(double, input)\nprintln(output[0])", "fn double(value:i32)->i32\nreturn value * 2\nend fn\n")},
    {"parallel_alias", program("let input: array<Word> = [1, 2]\nlet output = parallel_map(double, input)", "type Word = i32\nfn double(value:Word)->Word\nreturn value * 2\nend fn\n")},
    {"parallel_namespace_alias", program("let input:array<i32> = [1]\nlet output=parallel_map(alias::worker,input)", "module provider\npub fn worker(value:i32)->i32 { return value+1 }\nmodule consumer\nimport provider\nnamespace alias=provider\n")},
  };
  const std::vector<std::pair<std::string, std::string>> bad = {
    {"copy", program(owner + "let alias = buffer")},
    {"borrow_copy", program(owner + "let view = owned_borrow(buffer)\nlet alias = view")},
    {"use_after_move", program(owner + "let next = owned_move(buffer)\nprintln(*buffer)")},
    {"double_move", program(owner + "let a = owned_move(buffer)\nlet b = owned_move(buffer)")},
    {"branch_maybe_move", program(owner + "if true then\nlet next = owned_move(buffer)\nend if\nprintln(*buffer)")},
    {"else_maybe_move", program(owner + "if true then\nprintln(1)\nelse\nlet next = owned_move(buffer)\nend if\nprintln(*buffer)")},
    {"loop_outer_move", program(owner + "for index in range(2)\nlet next = owned_move(buffer)\nend for")},
    {"borrow_after_move", program(owner + "let next = owned_move(buffer)\nlet view = owned_borrow(buffer)")},
    {"move_while_borrowed", program(owner + "let view = owned_borrow(buffer)\nlet next = owned_move(buffer)")},
    {"mutate_immutable_borrow", program(owner + "let view = owned_borrow(buffer)\n*view = 7")},
    {"index_immutable_borrow", program(owner + "let view = owned_borrow(buffer)\nview[0] = 7")},
    {"mutate_shared_owner", program(owner + "let view = owned_borrow(buffer)\nbuffer[0] = 7")},
    {"exclusive_alias", program(owner + "let view = owned_borrow_mut(buffer)\nlet second = owned_borrow_mut(buffer)")},
    {"shared_then_exclusive", program(owner + "let view = owned_borrow(buffer)\nlet second = owned_borrow_mut(buffer)")},
    {"exclusive_then_shared", program(owner + "let view = owned_borrow_mut(buffer)\nlet second = owned_borrow(buffer)")},
    {"owner_read_during_mutable_borrow", program(owner + "let view = owned_borrow_mut(buffer)\nprintln(*buffer)")},
    {"owner_write_immutable", program("let buffer = owned<i32>(1)\n*buffer = 2")},
    {"borrow_mut_immutable", program("let buffer = owned<i32>(1)\nlet view = owned_borrow_mut(buffer)")},
    {"deallocate_owner", program(owner + "deallocate(buffer)")},
    {"deallocate_borrow", program(owner + "let view = owned_borrow(buffer)\ndeallocate(view)")},
    {"erase_any", program(owner + "let erased:any = buffer")},
    {"array_escape", program(owner + "let escaped = [buffer]")},
    {"call_escape", program(owner + "take(buffer)", "fn take(value:any)\nend fn\n")},
    {"return_owner", "fn create()->owned<i32>\nlet value=owned<i32>(1)\nreturn value\nend fn\n"},
    {"return_borrow", "fn create()->borrow<i32>\nlet value=owned<i32>(1)\nlet view=owned_borrow(value)\nreturn view\nend fn\n"},
    {"owner_param", "fn take(value:owned<i32>)\nend fn\n"},
    {"borrow_param", "fn take(value:borrow<i32>)\nend fn\n"},
    {"owner_field", "struct Bad\nvalue:owned<i32>\nend struct\n"},
    {"owner_field_alias", "type Buffer=owned<i32>\nstruct Bad\nvalue:Buffer\nend struct\n"},
    {"global_owner", "let buffer=owned<i32>(1)\nfn main()->i32\nreturn 0\nend fn\n"},
    {"owner_cast", program(owner + "let raw=buffer as ptr<i32>")},
    {"owner_element_address", program(owner + "let raw=&buffer[0]")},
    {"owner_deref_address", program(owner + "let raw=&*buffer")},
    {"raw_to_owner", program("let raw=allocate<i32>(1)\nlet forged=raw as owned<i32>")},
    {"owned_rebinding", program(owner + "buffer=owned<i32>(2)")},
    {"discard_owned", program("owned<i32>(1)")},
    {"temporary_borrow", program("let view=owned_borrow(owned<i32>(1))")},
    {"closure_capture", program(owner + "let closure=fn()->i32\nreturn *buffer\nend fn")},
    {"outer_fault_move", program(owner + "attempt\nlet moved=owned_move(buffer)\nrecover\nprintln(1)\nend attempt")},
    {"float_owned", program("let value=owned<f64>(1)")},
    {"nested_owned", program("let value=owned<owned<i32>>(1)")},
    {"wrong_count", program("let value=owned<i32>(true)")},
    {"parallel_local_worker", program("let worker=double\nlet input:array<i32> = [1]\nlet output=parallel_map(worker,input)", "fn double(value:i32)->i32 { return value*2 }\n")},
    {"parallel_mismatch", program("let input:array<i64> = [1]\nlet output=parallel_map(double,input)", "fn double(value:i32)->i32 { return value*2 }\n")},
    {"parallel_bad_output", program("let input:array<i32> = [1]\nlet output=parallel_map(worker,input)", "fn worker(value:i32)->text { return \"bad\" }\n")},
    {"parallel_bad_arity", program("let input:array<i32> = [1]\nlet output=parallel_map(worker,input)", "fn worker(a:i32,b:i32)->i32 { return a+b }\n")},
    {"parallel_ambiguous_globs", program("let input:array<i32> = [1]\nlet output=parallel_map(worker,input)", "module first\npub fn worker(value:i32)->i32 { return value+1 }\nmodule second\npub fn worker(value:i32)->i32 { return value+2 }\nmodule consumer\nimport first.*\nimport second.*\n")},
  };
  for (const auto &item : good) check(item.first, item.second, true);
  for (const auto &item : bad) check(item.first, item.second, false);
  std::cout << good.size() << " positive and " << bad.size() << " negative ownership/concurrency semantic fixtures\n";
  return failures ? 1 : 0;
}
