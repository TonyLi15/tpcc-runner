// Test for Type-casting
// core_id -> core_pos: uint64_t -> int

#include <iostream>
#include <typeinfo>

int main() {
    uint64_t test64 = 20;
    int testint = 64;
    int res = test64 % testint;
    std::cout << res << std::endl;
    std::cout << "Type of Result: " << typeid(res).name() << std::endl;
}