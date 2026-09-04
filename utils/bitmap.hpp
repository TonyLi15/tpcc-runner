#pragma once

int count_bits(uint64_t bits) { return __builtin_popcountll(bits); }

// 1000...0000
// 注： 符号付きの右シフトは左が符号ビットで埋まる
// int64_t set_upper_bit___signed() { return UINT64_MAX ^ (UINT64_MAX >>
// 1); }
int64_t set_upper_bit___signed() { return ~(~0ULL >> 1); }
uint64_t set_upper_bit___unsigned() { return ~(~0ULL >> 1); } // TODO

uint64_t set_bit_at_the_given_location(uint64_t pos) { // TODO
    return set_upper_bit___unsigned() >> pos;
}

bool is_bit_set_at_the_position(uint64_t bitmap, int pos) {
    return (bitmap & set_bit_at_the_given_location(pos)) != 0;
}

uint64_t set_bit_at_the_given_location(uint64_t bitmap, uint64_t pos) {
    assert(pos < 64); // TODO
    assert(!is_bit_set_at_the_position(bitmap, pos));
    return bitmap | set_bit_at_the_given_location(pos);
}

uint64_t fill_the_left_side_until_before_the_given_position(int pos) {
    /*
    pos: 0
    0000 0000 0000 0000
    */
    if (pos == 0)
        return 0;

    /*
    pos: 1
    1000 0000 0000 0000

    pos: 2
    1100 0000 0000 0000

    pos: 3
    1110 0000 0000 0000
    */
    return set_upper_bit___signed() >> (pos - 1);
}

uint64_t fill_the_left_side_until_the_given_position(int pos) {
    /*
    pos: 0
    1000 0000 0000 0000

    pos: 1
    1100 0000 0000 0000

    pos: 2
    1110 0000 0000 0000

    pos: 3
    1111 0000 0000 0000
    */
    return set_upper_bit___signed() >> pos;
}

// returns the location of 1st "1" from right
int find_the_location_of_first_bit_from_right(uint64_t bits) {
    return __builtin_ffsll(bits);
}

// TODO: refactor the name
int find_the_largest(uint64_t bits) {
    assert(bits != 0);
    int result = 64 - find_the_location_of_first_bit_from_right(bits);
    assert(0 <= result && result < 64);
    return result;
}

int find_the_largest_among_or_less_than(uint64_t bitmap, int pos) {
    assert(bitmap != 0);
    assert(0 <= pos && pos < 64);
    uint64_t smallers =
        bitmap & fill_the_left_side_until_the_given_position(pos);
    if (smallers == 0)
        return -1;
    return find_the_largest(smallers);
}

// {second largest, first largest}
std::pair<int, int> find_the_two_largest_among_or_less_than(uint64_t bitmap,
                                                            int pos) {
    assert(bitmap != 0);
    assert(0 <= pos && pos < 64);
    uint64_t smallers =
        bitmap & fill_the_left_side_until_the_given_position(pos);
    if (smallers == 0)
        return {-1, -1};
    int largest_pos = find_the_largest(smallers);
    assert(largest_pos < 64);
    if (__builtin_popcountll(smallers) == 1) {
        return {-1, largest_pos};
    }
    assert(0 <= largest_pos);
    int second_largest_pos = find_the_largest(
        ~set_bit_at_the_given_location(largest_pos) & smallers);
    assert(second_largest_pos < largest_pos);
    return {second_largest_pos, largest_pos};
}

int find_the_largest_among_less_than(uint64_t bitmap, int pos) {
    assert(bitmap != 0);
    assert(0 <= pos && pos < 64);
    uint64_t smallers =
        bitmap & fill_the_left_side_until_before_the_given_position(pos);
    if (smallers == 0)
        return -1;
    return find_the_largest(smallers);
}

// ---- __uint128_t overloads (used when NUM_CORE > 64) ----

int count_bits(__uint128_t bits) {
    return __builtin_popcountll((uint64_t)(bits >> 64)) + __builtin_popcountll((uint64_t)bits);
}

__uint128_t set_upper_bit_128() { return (__uint128_t)1 << 127; }

__uint128_t set_bit_at_the_given_location_128(int pos) {
    return set_upper_bit_128() >> pos;
}

bool is_bit_set_at_the_position(__uint128_t bitmap, int pos) {
    return (bitmap & set_bit_at_the_given_location_128(pos)) != 0;
}

__uint128_t set_bit_at_the_given_location(__uint128_t bitmap, uint64_t pos) {
    assert(pos < 128);
    assert(!is_bit_set_at_the_position(bitmap, (int)pos));
    return bitmap | set_bit_at_the_given_location_128((int)pos);
}

__uint128_t fill_the_left_side_until_before_the_given_position_128(int pos) {
    if (pos == 0) return 0;
    return (__int128_t)set_upper_bit_128() >> (pos - 1);
}

__uint128_t fill_the_left_side_until_the_given_position_128(int pos) {
    return (__int128_t)set_upper_bit_128() >> pos;
}

int find_the_location_of_first_bit_from_right_128(__uint128_t bits) {
    uint64_t lo = (uint64_t)bits;
    if (lo != 0) return __builtin_ffsll(lo);
    uint64_t hi = (uint64_t)(bits >> 64);
    return __builtin_ffsll(hi) + 64;
}

int find_the_largest(__uint128_t bits) {
    assert(bits != 0);
    int result = 128 - find_the_location_of_first_bit_from_right_128(bits);
    assert(0 <= result && result < 128);
    return result;
}

std::pair<int, int> find_the_two_largest_among_or_less_than(__uint128_t bitmap, int pos) {
    assert(bitmap != 0);
    assert(0 <= pos && pos < 128);
    __uint128_t smallers = bitmap & fill_the_left_side_until_the_given_position_128(pos);
    if (smallers == 0) return {-1, -1};
    int largest_pos = find_the_largest(smallers);
    assert(largest_pos < 128);
    if (count_bits(smallers) == 1) {
        return {-1, largest_pos};
    }
    assert(0 <= largest_pos);
    int second_largest_pos = find_the_largest(
        ~set_bit_at_the_given_location_128(largest_pos) & smallers);
    assert(second_largest_pos < largest_pos);
    return {second_largest_pos, largest_pos};
}

int find_the_largest_among_less_than(__uint128_t bitmap, int pos) {
    assert(bitmap != 0);
    assert(0 <= pos && pos < 128);
    __uint128_t smallers =
        bitmap & fill_the_left_side_until_before_the_given_position_128(pos);
    if (smallers == 0) return -1;
    return find_the_largest(smallers);
}
