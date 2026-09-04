#pragma once

#include "protocols/serval_rc_bbu/include/rw_bitmaps.hpp"

struct Value {
    alignas(64) WriteBitmap w_bitmap_;
};
