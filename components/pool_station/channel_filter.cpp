#include "channel_filter.h"
#include <numeric>

namespace esphome {
namespace pool_station {

// ============================================================================
// SlidingWindow
// ============================================================================

void SlidingWindow::set_size(uint8_t size) {
  this->size_ = size;
  this->buffer_.resize(size, NAN);
  this->scratch_.clear();
  this->scratch_.reserve(size);
  this->head_ = 0;
  this->count_ = 0;
}

void SlidingWindow::push(float value) {
  if (this->size_ == 0) {
    return;
  }
  
  this->buffer_[this->head_] = value;
  this->head_ = (this->head_ + 1) % this->size_;
  
  if (this->count_ < this->size_) {
    this->count_++;
  }
}

void SlidingWindow::clear() {
  this->head_ = 0;
  this->count_ = 0;
  std::fill(this->buffer_.begin(), this->buffer_.end(), NAN);
}

float SlidingWindow::median() const {
  if (this->count_ == 0) {
    return NAN;
  }

  // Reuse scratch_ — reserve once in set_size(); clear does not free capacity.
  this->scratch_.clear();
  for (size_t i = 0; i < this->count_; i++) {
    float v = this->buffer_[i];
    if (!std::isnan(v)) {
      this->scratch_.push_back(v);
    }
  }

  return filter_functions::compute_median(this->scratch_);
}

float SlidingWindow::mean() const {
  if (this->count_ == 0) {
    return NAN;
  }

  // Allocation-free mean (no scratch needed).
  float sum = 0.0f;
  size_t n = 0;
  for (size_t i = 0; i < this->count_; i++) {
    float v = this->buffer_[i];
    if (!std::isnan(v)) {
      sum += v;
      n++;
    }
  }
  return n > 0 ? sum / static_cast<float>(n) : NAN;
}

float SlidingWindow::min() const {
  if (this->count_ == 0) {
    return NAN;
  }
  
  float result = std::numeric_limits<float>::max();
  bool found = false;
  
  for (size_t i = 0; i < this->count_; i++) {
    float v = this->buffer_[i];
    if (!std::isnan(v)) {
      result = std::min(result, v);
      found = true;
    }
  }
  
  return found ? result : NAN;
}

float SlidingWindow::max() const {
  if (this->count_ == 0) {
    return NAN;
  }
  
  float result = std::numeric_limits<float>::lowest();
  bool found = false;
  
  for (size_t i = 0; i < this->count_; i++) {
    float v = this->buffer_[i];
    if (!std::isnan(v)) {
      result = std::max(result, v);
      found = true;
    }
  }
  
  return found ? result : NAN;
}

// ============================================================================
// JumpGuard
// ============================================================================

void JumpGuard::reset() {
  this->last_accepted_ = NAN;
  this->pending_value_ = NAN;
  this->streak_count_ = 0;
  this->has_baseline_ = false;
}

float JumpGuard::process(float value, bool &rejected_out) {
  rejected_out = false;
  
  if (std::isnan(value)) {
    return this->last_accepted_;
  }
  
  if (!this->has_baseline_) {
    this->last_accepted_ = value;
    this->has_baseline_ = true;
    this->streak_count_ = 0;
    return value;
  }
  
  if (this->max_jump_ <= 0.0f) {
    this->last_accepted_ = value;
    return value;
  }
  
  bool jump_exceeded = filter_functions::is_jump_exceeded(
      value, this->last_accepted_, this->max_jump_);
  
  if (!jump_exceeded) {
    this->last_accepted_ = value;
    this->streak_count_ = 0;
    this->pending_value_ = NAN;
    return value;
  }
  
  rejected_out = true;
  
  if (std::isnan(this->pending_value_) || 
      filter_functions::is_jump_exceeded(value, this->pending_value_, this->max_jump_)) {
    this->pending_value_ = value;
    this->streak_count_ = 1;
  } else {
    this->streak_count_++;
    this->pending_value_ = value;
  }
  
  if (this->streak_count_ >= this->streak_threshold_) {
    this->last_accepted_ = value;
    this->streak_count_ = 0;
    this->pending_value_ = NAN;
    rejected_out = false;
    return value;
  }
  
  return this->last_accepted_;
}

// ============================================================================
// Pure filter functions
// ============================================================================

namespace filter_functions {

float compute_median(std::vector<float> &values) {
  if (values.empty()) {
    return NAN;
  }

  values.erase(
      std::remove_if(values.begin(), values.end(),
                     [](float v) { return std::isnan(v); }),
      values.end());

  if (values.empty()) {
    return NAN;
  }

  size_t n = values.size();
  size_t mid = n / 2;

  std::nth_element(values.begin(), values.begin() + mid, values.end());
  float median = values[mid];

  if (n % 2 == 0) {
    std::nth_element(values.begin(), values.begin() + mid - 1, values.end());
    median = (median + values[mid - 1]) / 2.0f;
  }

  return median;
}

float compute_mean(const std::vector<float> &values) {
  if (values.empty()) {
    return NAN;
  }
  
  float sum = 0.0f;
  size_t count = 0;
  
  for (float v : values) {
    if (!std::isnan(v)) {
      sum += v;
      count++;
    }
  }
  
  return count > 0 ? sum / static_cast<float>(count) : NAN;
}

float clamp_value(float value, float min_val, float max_val) {
  if (std::isnan(value)) {
    return value;
  }
  
  if (!std::isnan(min_val) && value < min_val) {
    return min_val;
  }
  
  if (!std::isnan(max_val) && value > max_val) {
    return max_val;
  }
  
  return value;
}

bool is_jump_exceeded(float current, float previous, float threshold) {
  if (std::isnan(current) || std::isnan(previous) || threshold <= 0.0f) {
    return false;
  }
  
  return std::abs(current - previous) > threshold;
}

}  // namespace filter_functions

}  // namespace pool_station
}  // namespace esphome
