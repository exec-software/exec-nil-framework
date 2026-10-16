#ifndef NY_COMPILER_OWNED_CONTRACT_HPP
#define NY_COMPILER_OWNED_CONTRACT_HPP
#include <vector>
namespace ny {
struct nil_module;
struct Instruction;
// Resource-protocol validation, independent of ordinary raw-pointer safety.
void validate_nil_owned_contract(const nil_module &module);
void validate_nil_owned_contract(const std::vector<Instruction> &program);
}
#endif
