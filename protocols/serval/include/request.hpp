#pragma once

#include <vector>

class Operation {
 public:
  enum Ope { Read, Update };
  Ope ope_;
  int index_;
  int value_ = 0;

  Operation(Ope operation, int idx) : ope_(operation), index_(idx) {}
};

class Request {
 public:
  std::vector<Operation> operations_;
  std::vector<Operation> write_set_;
};