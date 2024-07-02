#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

#include "protocols/common/readwritelock.hpp"
#include "protocols/serval_rc/include/version.hpp"
#include "utils/bitmap.hpp"

class WriteBitmap {
  public:
    RWLock lock_;
    uint64_t ref_cnt_ = 0;
    uint64_t core_bitmap_ = 0;
    Version *master_ = nullptr;
    std::vector<uint64_t>
        tx_bitmaps_; // written when write phase, read when read phase
    std::vector<std::pair<uint64_t, Version *>>
        placeholders_; // written when read phase, read when exec phase
    // ascending order

    // write phase
    void update_bitmap(uint64_t core, uint64_t txid) {
        lock_.lock();
        int insert_pos = count_prefix_sum(core);
        if (is_bit_set_at_the_position(core_bitmap_, core)) {
            update_tx_bitmap(insert_pos, txid);
        } else {
            insert_tx_bitmap(insert_pos, txid);
            update_core_bitmap(core);
        }
        lock_.unlock();
    }

    // read phase
    Version *append_pending_version(uint64_t core, uint64_t tx) {
        lock_.lock();
        ref_cnt_++;
        Version *v = do_append_pending_version(core, tx);
        lock_.unlock();
        return v;
    }

    // execute read
    void decrement_ref_cnt() {
        lock_.lock();
        ref_cnt_--;
        if (ref_cnt_ == 0) {
            gc_and_update_master();
        }
        lock_.unlock();
    }

    // execute write
    Version *identify_write_version(uint64_t core, uint64_t tx) {
        Version *write_v = nullptr;
        lock_.lock();
        if (ref_cnt_) {
            uint64_t serial_id = get_serial_id(core, tx);

            // search placeholders.
            // if my_txid is in placeholders, execute write.
            auto itr = std::find_if(placeholders_.begin(), placeholders_.end(),
                                    [serial_id](const auto &pair) {
                                        return pair.first == serial_id;
                                    });
            if (itr != placeholders_.end()) {
                write_v = itr->second; // visible version
            }
        } else {
            assert(placeholders_.empty());
            // if my_txid is final state, update master
            if (is_final_state(core, tx)) {
                gc_master();
                master_ = create_pending_version();
                write_v = master_;
            }
        }
        lock_.unlock();

        return write_v;
    }

  private:
    void gc_master() {
        assert(master_);
        assert(master_->status == Version::VersionStatus::STABLE);
        delete reinterpret_cast<Record *>(master_->rec);
        delete master_;
    }

    uint64_t get_serial_id(uint64_t core, uint64_t tx) {
        return core * 64 + tx;
    }

    void clear_bitmaps() {
        core_bitmap_ = 0;
        tx_bitmaps_.clear();
    }
    // execute write
    bool is_final_state(uint64_t core, uint64_t tx) {
        bool is_final = false;
        if (core_bitmap_) {
            uint64_t last_core = find_the_largest(core_bitmap_);
            if (last_core == core) {
                uint64_t last_tx = find_the_largest(tx_bitmaps_.back());

                if (last_tx == tx)
                    is_final = true;
            }
            clear_bitmaps();
        }
        return is_final;
    }

    void update_tx_bitmap(int pos, uint64_t tx) {
        assert(pos < (int)tx_bitmaps_.size());
        tx_bitmaps_[pos] = set_bit_at_the_given_location(tx_bitmaps_[pos], tx);
    }
    void insert_tx_bitmap(int insert_pos, uint64_t tx) {
        tx_bitmaps_.insert(tx_bitmaps_.begin() + insert_pos,
                           set_bit_at_the_given_location(tx));
    }
    void update_core_bitmap(uint64_t core) {
        core_bitmap_ = set_bit_at_the_given_location(core_bitmap_, core);
    }
    int count_prefix_sum(uint64_t core) {
        /*
        core: 4
        0010 [1]000 ...: prefix_sum　is 1

        core: 4
        0011 [1]000 ...: prefix_sum　is is 2
        */
        return count_bits(
            core_bitmap_ &
            fill_the_left_side_until_before_the_given_position(core));
    }

    // read phase
    std::tuple<bool, uint64_t, uint64_t>
    identify_visible_version_in_placeholders(uint64_t core, uint64_t tx) {
        auto [second_core, first_core] =
            find_the_two_largest_among_or_less_than(core_bitmap_, core);

        if (first_core == -1) {
            assert(first_core == -1 && second_core == -1);
            return {false, 0, 0};
        }

        int first_core_pos = count_prefix_sum(first_core);
        assert(first_core_pos < (int)tx_bitmaps_.size());

        if (first_core == (int)core) {
            int first_tx = find_the_largest_among_less_than(
                tx_bitmaps_[first_core_pos], tx);

            if (first_tx != -1) {
                return {true, first_core, first_tx};
            }

            if (second_core != -1) {
                int second_core_pos = count_prefix_sum(second_core);
                assert(second_core_pos < first_core_pos);
                return {true, second_core,
                        find_the_largest(tx_bitmaps_[second_core_pos])};
            }

            return {false, 0, 0};
        }

        assert(0 <= first_core && (uint64_t)first_core < core);
        return {true, first_core,
                find_the_largest(tx_bitmaps_[first_core_pos])};
    }

    // read phase
    Version *do_append_pending_version(uint64_t core, uint64_t tx) {
        if (core_bitmap_) {
            auto [is_found, first_core, first_tx] =
                identify_visible_version_in_placeholders(core, tx);

            if (is_found) {
                uint64_t visible_id = get_serial_id(first_core, first_tx);
                auto itr =
                    std::find_if(placeholders_.begin(), placeholders_.end(),
                                 [visible_id](const auto &pair) {
                                     return pair.first == visible_id;
                                 });
                // placeholders_の中にまだvisible_idは存在しなかった
                if (itr == placeholders_.end()) {
                    // Find the position to insert the new element
                    itr = std::upper_bound(
                        placeholders_.begin(), placeholders_.end(),
                        std::make_pair(visible_id, nullptr),
                        [](const auto &pair, const auto &new_pair) {
                            return pair.first < new_pair.first;
                        });
                    // Insert the new element at the found position
                    itr = placeholders_.insert(
                        itr,
                        std::make_pair(visible_id, create_pending_version()));
                }
                return itr->second;
            }
        }

        return master_;
    }

    // execute read
    void gc_and_update_master() {
        if (!placeholders_.empty()) {
            // update master version
            gc_master();
            master_ = placeholders_.back().second;
            placeholders_.clear();
        }
        clear_bitmaps();
    }

    Version *create_pending_version() {
        Version *version = new Version;
        version->rec = nullptr; // TODO
        version->deleted = false;
        version->status = Version::VersionStatus::PENDING;
        return version;
    }
};