#pragma once

#include <cassert>
#include <cstdint>
#include <vector>

#include "protocols/caracal/include/row_buffer.hpp"
#include "protocols/caracal/include/version.hpp"
#include "protocols/ycsb_common/definitions.hpp"

class MajorGC {
  public:
    void collect(uint64_t cur_epoc, Version *version,
                 GlobalVersionArray &array) {
        versions_.emplace_back(cur_epoc, version, array);
    }

    void major_gc(uint64_t cur_epoch) {
        if (versions_.empty())
            return;

        auto itr = versions_.begin();
        while (itr != versions_.end()) {
            auto [id, version, array] = *itr;
            if ((cur_epoch - k_) <= id)
                break;

            array.lock();
            array.major_gc(cur_epoch - k_);
            array.unlock();

            itr = versions_.erase(itr);
        }
    }

  private:
    static const int k_ = 4;

    std::vector<std::tuple<uint64_t, Version *, GlobalVersionArray &>>
        versions_;
};