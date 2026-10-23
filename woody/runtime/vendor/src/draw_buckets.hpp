#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <new>

// Stable insertion keeps translucent sprites in their original submission order.
template <std::size_t BucketCount> class DrawBuckets {
    std::array<int, BucketCount> head_{};
    std::array<int, BucketCount> tail_{};
    std::unique_ptr<int[]> next_;
    std::size_t capacity_ = 0;

  public:
    bool reset(std::size_t count) {
        if (count > capacity_) {
            std::unique_ptr<int[]> replacement(new (std::nothrow) int[count]);
            if (!replacement)
                return false;
            next_ = std::move(replacement);
            capacity_ = count;
        }
        head_.fill(-1);
        tail_.fill(-1);
        return true;
    }

    void append(std::size_t bucket, int index) {
        next_[index] = -1;
        if (tail_[bucket] >= 0)
            next_[tail_[bucket]] = index;
        else
            head_[bucket] = index;
        tail_[bucket] = index;
    }

    int first(std::size_t bucket) const {
        return head_[bucket];
    }
    int next(int index) const {
        return next_[index];
    }
};
