#ifndef NY_COMPILER_CONCURRENCY_CONTRACT_HPP
#define NY_COMPILER_CONCURRENCY_CONTRACT_HPP

#include <string>
#include <unordered_set>
#include <vector>

namespace ny {
struct nil_module;
struct Instruction;
bool is_parallel_scalar(const std::string &type);
void validate_nil_parallel_contract(const nil_module &module);
void validate_nil_parallel_contract(const std::vector<Instruction> &program);
std::unordered_set<std::string>
collect_nil_parallel_workers(const nil_module &module);
} // namespace ny
#endif
