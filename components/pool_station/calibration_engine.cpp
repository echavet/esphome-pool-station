#include "calibration_engine.h"

namespace esphome {
namespace pool_station {

const char *CalibrationEngine::get_type_name() const {
  return calibration_type_name(this->type_);
}

// ============================================================================
// Configuration (Lot 7: with validation callback)
// ============================================================================

void CalibrationEngine::set_type(CalibrationType type) {
  this->type_ = type;
  this->poly_coeffs_valid_ = false;
}

void CalibrationEngine::set_type_runtime(CalibrationType type) {
  if (this->type_ == type) {
    return;
  }
  
  CalibrationType old_type = this->type_;
  this->type_ = type;
  this->poly_coeffs_valid_ = false;
  
  ESP_LOGI(CAL_TAG, "Calibration algorithm changed: %s -> %s", 
           calibration_type_name(old_type), calibration_type_name(type));
  
  // Revalidate and notify
  this->notify_validation_changed_();
}

void CalibrationEngine::notify_validation_changed_() {
  if (this->validation_callback_) {
    bool valid = this->is_valid();
    this->validation_callback_(valid);
  }
}

// ============================================================================
// Point management (Lot 7: operates on appropriate set)
// ============================================================================

void CalibrationEngine::add_point(float x, float y) {
  auto &points = this->points_();
  if (points.size() >= MAX_CALIBRATION_POINTS) {
    ESP_LOGW(CAL_TAG, "Max calibration points (%d) reached, ignoring", MAX_CALIBRATION_POINTS);
    return;
  }
  
  CalibrationPoint pt;
  pt.x = x;
  pt.y = y;
  points.push_back(pt);
  this->poly_coeffs_valid_ = false;
  
  // Keep points sorted by x for piecewise
  std::sort(points.begin(), points.end(), CalibrationPoint::compare_by_x);
  
  if (this->draft_mode_enabled_) {
    this->has_draft_changes_ = true;
  }
  
  ESP_LOGD(CAL_TAG, "Added calibration point: x=%.4f, y=%.4f (total: %zu)%s", 
           x, y, points.size(), this->draft_mode_enabled_ ? " [DRAFT]" : "");
  
  this->notify_validation_changed_();
}

void CalibrationEngine::set_point(size_t index, float x, float y) {
  auto &points = this->points_();
  if (index >= points.size()) {
    ESP_LOGW(CAL_TAG, "Point index %zu out of range (size: %zu)", index, points.size());
    return;
  }
  
  points[index].x = x;
  points[index].y = y;
  this->poly_coeffs_valid_ = false;
  
  // Re-sort after modification
  std::sort(points.begin(), points.end(), CalibrationPoint::compare_by_x);
  
  if (this->draft_mode_enabled_) {
    this->has_draft_changes_ = true;
  }
  
  ESP_LOGD(CAL_TAG, "Updated calibration point %zu: x=%.4f, y=%.4f%s", 
           index, x, y, this->draft_mode_enabled_ ? " [DRAFT]" : "");
  
  this->notify_validation_changed_();
}

void CalibrationEngine::remove_point(size_t index) {
  auto &points = this->points_();
  if (index >= points.size()) {
    return;
  }
  
  points.erase(points.begin() + index);
  this->poly_coeffs_valid_ = false;
  
  if (this->draft_mode_enabled_) {
    this->has_draft_changes_ = true;
  }
  
  ESP_LOGD(CAL_TAG, "Removed calibration point %zu (remaining: %zu)%s", 
           index, points.size(), this->draft_mode_enabled_ ? " [DRAFT]" : "");
  
  this->notify_validation_changed_();
}

void CalibrationEngine::clear_points() {
  auto &points = this->points_();
  points.clear();
  this->poly_coeffs_valid_ = false;
  
  if (this->draft_mode_enabled_) {
    this->has_draft_changes_ = true;
  }
  
  ESP_LOGD(CAL_TAG, "Cleared all calibration points%s", 
           this->draft_mode_enabled_ ? " [DRAFT]" : "");
  
  this->notify_validation_changed_();
}

size_t CalibrationEngine::get_point_count() const {
  return this->points_().size();
}

CalibrationPoint CalibrationEngine::get_point(size_t index) const {
  const auto &points = this->points_();
  if (index >= points.size()) {
    return CalibrationPoint();  // Returns invalid point (NAN values)
  }
  return points[index];
}

const std::vector<CalibrationPoint> &CalibrationEngine::get_points() const {
  return this->points_();
}

void CalibrationEngine::set_seed_points(const std::vector<CalibrationPoint> &points) {
  // Seed points go to live set
  this->live_points_ = points;
  this->poly_coeffs_valid_ = false;
  
  // Sort by x
  std::sort(this->live_points_.begin(), this->live_points_.end(), CalibrationPoint::compare_by_x);
  
  // If draft mode is enabled, also copy to draft
  if (this->draft_mode_enabled_) {
    this->draft_points_ = this->live_points_;
    this->has_draft_changes_ = false;
  }
  
  ESP_LOGD(CAL_TAG, "Set %zu seed calibration points", this->live_points_.size());
}

// ============================================================================
// Draft/Commit workflow (Lot 7)
// ============================================================================

void CalibrationEngine::enable_draft_mode() {
  if (this->draft_mode_enabled_) {
    return;
  }
  
  // Copy live points to draft
  this->draft_points_ = this->live_points_;
  this->draft_mode_enabled_ = true;
  this->has_draft_changes_ = false;
  
  ESP_LOGI(CAL_TAG, "Draft mode ENABLED (copied %zu live points to draft)", this->live_points_.size());
}

void CalibrationEngine::disable_draft_mode() {
  if (!this->draft_mode_enabled_) {
    return;
  }
  
  this->draft_mode_enabled_ = false;
  
  if (this->has_draft_changes_) {
    ESP_LOGW(CAL_TAG, "Draft mode DISABLED with uncommitted changes (discarded)");
    this->has_draft_changes_ = false;
  } else {
    ESP_LOGI(CAL_TAG, "Draft mode DISABLED");
  }
  
  this->draft_points_.clear();
}

void CalibrationEngine::commit_draft() {
  if (!this->draft_mode_enabled_) {
    ESP_LOGW(CAL_TAG, "Cannot commit: draft mode not enabled");
    return;
  }
  
  // Copy draft to live
  this->live_points_ = this->draft_points_;
  this->has_draft_changes_ = false;
  this->poly_coeffs_valid_ = false;
  
  ESP_LOGI(CAL_TAG, "Committed %zu draft points to live calibration", this->live_points_.size());
  
  // Save to preferences
  this->save_to_preferences();
  
  this->notify_validation_changed_();
}

void CalibrationEngine::discard_draft() {
  if (!this->draft_mode_enabled_) {
    ESP_LOGW(CAL_TAG, "Cannot discard: draft mode not enabled");
    return;
  }
  
  // Reset draft to match live
  this->draft_points_ = this->live_points_;
  this->has_draft_changes_ = false;
  
  ESP_LOGI(CAL_TAG, "Discarded draft changes, reverted to %zu live points", this->live_points_.size());
  
  this->notify_validation_changed_();
}

uint8_t CalibrationEngine::get_minimum_points_for_type(CalibrationType type) const {
  switch (type) {
    case CAL_TYPE_NONE:
      return 0;
    case CAL_TYPE_LINEAR:
      return 2;
    case CAL_TYPE_POLYNOMIAL:
      // Polynomial of order N needs N+1 points minimum
      return this->polynomial_order_ + 1;
    case CAL_TYPE_PIECEWISE:
      return 2;  // At least 2 points for one segment
    case CAL_TYPE_DFROBOT_ORP:
      return 0;  // Uses mid/offset, points are optional for offset calc
    case CAL_TYPE_EXPONENTIAL:
    case CAL_TYPE_LOGARITHMIC:
    case CAL_TYPE_POWER:
      return 2;  // TODO: implement these
    default:
      return 0;
  }
}

uint8_t CalibrationEngine::get_minimum_points() const {
  return this->get_minimum_points_for_type(this->type_);
}

bool CalibrationEngine::is_valid() const {
  // Uses LIVE points for validation (actual calibration state)
  if (this->type_ == CAL_TYPE_NONE) {
    return false;
  }
  
  if (this->type_ == CAL_TYPE_DFROBOT_ORP) {
    // DFRobot ORP is always valid (uses mid/offset params)
    return true;
  }
  
  // Check minimum points against live set
  uint8_t min_pts = this->get_minimum_points();
  if (this->live_points_.size() < min_pts) {
    return false;
  }
  
  // Check all live points are valid
  for (const auto &pt : this->live_points_) {
    if (!pt.is_valid()) {
      return false;
    }
  }
  
  return true;
}

bool CalibrationEngine::is_draft_valid() const {
  // Validates the draft set (for UI feedback)
  if (this->type_ == CAL_TYPE_NONE) {
    return false;
  }
  
  if (this->type_ == CAL_TYPE_DFROBOT_ORP) {
    return true;
  }
  
  uint8_t min_pts = this->get_minimum_points();
  const auto &points = this->draft_mode_enabled_ ? this->draft_points_ : this->live_points_;
  
  if (points.size() < min_pts) {
    return false;
  }
  
  for (const auto &pt : points) {
    if (!pt.is_valid()) {
      return false;
    }
  }
  
  return true;
}

float CalibrationEngine::calibrate(float raw_voltage) const {
  if (std::isnan(raw_voltage)) {
    return NAN;
  }
  
  switch (this->type_) {
    case CAL_TYPE_NONE:
      return raw_voltage;  // Pass-through
      
    case CAL_TYPE_LINEAR:
      return this->calibrate_linear_(raw_voltage);
      
    case CAL_TYPE_POLYNOMIAL:
      return this->calibrate_polynomial_(raw_voltage);
      
    case CAL_TYPE_PIECEWISE:
      return this->calibrate_piecewise_(raw_voltage);
      
    case CAL_TYPE_DFROBOT_ORP:
      return this->calibrate_dfrobot_orp_(raw_voltage);
      
    case CAL_TYPE_EXPONENTIAL:
    case CAL_TYPE_LOGARITHMIC:
    case CAL_TYPE_POWER:
      // TODO: implement these algorithms
      ESP_LOGW(CAL_TAG, "Calibration type '%s' not yet implemented, using pass-through",
               this->get_type_name());
      return raw_voltage;
      
    default:
      return raw_voltage;
  }
}

float CalibrationEngine::calibrate_linear_(float x) const {
  // Always use live points for actual calibration
  if (this->live_points_.size() < 2) {
    ESP_LOGW(CAL_TAG, "Linear calibration needs 2 points, have %zu", this->live_points_.size());
    return x;
  }
  
  // Use first and last points for linear
  const CalibrationPoint &p1 = this->live_points_.front();
  const CalibrationPoint &p2 = this->live_points_.back();
  
  if (std::abs(p2.x - p1.x) < 1e-9f) {
    ESP_LOGW(CAL_TAG, "Linear calibration: points have same x value");
    return p1.y;
  }
  
  // y = y1 + (x - x1) * (y2 - y1) / (x2 - x1)
  float slope = (p2.y - p1.y) / (p2.x - p1.x);
  float result = p1.y + (x - p1.x) * slope;
  
  return result;
}

float CalibrationEngine::calibrate_polynomial_(float x) const {
  if (!this->poly_coeffs_valid_) {
    this->compute_polynomial_coefficients_();
  }
  
  if (this->poly_coeffs_.empty()) {
    ESP_LOGW(CAL_TAG, "Polynomial coefficients not computed");
    return x;
  }
  
  // Evaluate polynomial: y = c0 + c1*x + c2*x^2 + ... + cn*x^n
  float result = 0.0f;
  float x_power = 1.0f;
  
  for (size_t i = 0; i < this->poly_coeffs_.size(); i++) {
    result += this->poly_coeffs_[i] * x_power;
    x_power *= x;
  }
  
  return result;
}

void CalibrationEngine::compute_polynomial_coefficients_() const {
  this->poly_coeffs_.clear();
  this->poly_coeffs_valid_ = false;
  
  // Always use live points for actual calibration
  size_t n = this->live_points_.size();
  uint8_t order = this->polynomial_order_;
  
  if (n < order + 1) {
    ESP_LOGW(CAL_TAG, "Polynomial order %d needs %d points, have %zu", order, order + 1, n);
    return;
  }
  
  // Limit order to prevent numerical instability
  if (order > 5) {
    ESP_LOGW(CAL_TAG, "Polynomial order %d too high, limiting to 5", order);
    order = 5;
  }
  
  size_t terms = order + 1;
  
  // Build normal equations for least squares: (X^T * X) * coeffs = X^T * y
  // Using Gaussian elimination for simplicity
  
  // Compute X^T * X matrix and X^T * y vector
  std::vector<std::vector<float>> matrix(terms, std::vector<float>(terms + 1, 0.0f));
  
  for (size_t i = 0; i < terms; i++) {
    for (size_t j = 0; j < terms; j++) {
      float sum = 0.0f;
      for (size_t k = 0; k < n; k++) {
        float xi = this->live_points_[k].x;
        sum += std::pow(xi, (float)(i + j));
      }
      matrix[i][j] = sum;
    }
    
    // X^T * y
    float sum = 0.0f;
    for (size_t k = 0; k < n; k++) {
      float xi = this->live_points_[k].x;
      float yi = this->live_points_[k].y;
      sum += yi * std::pow(xi, (float)i);
    }
    matrix[i][terms] = sum;
  }
  
  // Gaussian elimination with partial pivoting
  for (size_t col = 0; col < terms; col++) {
    // Find pivot
    size_t max_row = col;
    float max_val = std::abs(matrix[col][col]);
    for (size_t row = col + 1; row < terms; row++) {
      if (std::abs(matrix[row][col]) > max_val) {
        max_val = std::abs(matrix[row][col]);
        max_row = row;
      }
    }
    
    if (max_val < 1e-10f) {
      ESP_LOGW(CAL_TAG, "Polynomial matrix is singular, cannot solve");
      return;
    }
    
    // Swap rows
    if (max_row != col) {
      std::swap(matrix[col], matrix[max_row]);
    }
    
    // Eliminate
    for (size_t row = col + 1; row < terms; row++) {
      float factor = matrix[row][col] / matrix[col][col];
      for (size_t j = col; j <= terms; j++) {
        matrix[row][j] -= factor * matrix[col][j];
      }
    }
  }
  
  // Back substitution
  this->poly_coeffs_.resize(terms, 0.0f);
  for (int i = (int)terms - 1; i >= 0; i--) {
    float sum = matrix[i][terms];
    for (size_t j = i + 1; j < terms; j++) {
      sum -= matrix[i][j] * this->poly_coeffs_[j];
    }
    this->poly_coeffs_[i] = sum / matrix[i][i];
  }
  
  this->poly_coeffs_valid_ = true;
  
  ESP_LOGD(CAL_TAG, "Computed polynomial coefficients (order %d):", order);
  for (size_t i = 0; i < this->poly_coeffs_.size(); i++) {
    ESP_LOGD(CAL_TAG, "  c%zu = %.6f", i, this->poly_coeffs_[i]);
  }
}

float CalibrationEngine::calibrate_piecewise_(float x) const {
  // Always use live points for actual calibration
  if (this->live_points_.size() < 2) {
    ESP_LOGW(CAL_TAG, "Piecewise calibration needs at least 2 points, have %zu", this->live_points_.size());
    return x;
  }
  
  // Points are sorted by x
  // Find the segment containing x
  
  // Below first point: extrapolate from first segment
  if (x <= this->live_points_.front().x) {
    const CalibrationPoint &p1 = this->live_points_[0];
    const CalibrationPoint &p2 = this->live_points_[1];
    float slope = (p2.y - p1.y) / (p2.x - p1.x);
    return p1.y + (x - p1.x) * slope;
  }
  
  // Above last point: extrapolate from last segment
  if (x >= this->live_points_.back().x) {
    size_t n = this->live_points_.size();
    const CalibrationPoint &p1 = this->live_points_[n - 2];
    const CalibrationPoint &p2 = this->live_points_[n - 1];
    float slope = (p2.y - p1.y) / (p2.x - p1.x);
    return p2.y + (x - p2.x) * slope;
  }
  
  // Find segment [i, i+1] where live_points_[i].x <= x < live_points_[i+1].x
  for (size_t i = 0; i < this->live_points_.size() - 1; i++) {
    const CalibrationPoint &p1 = this->live_points_[i];
    const CalibrationPoint &p2 = this->live_points_[i + 1];
    
    if (x >= p1.x && x <= p2.x) {
      if (std::abs(p2.x - p1.x) < 1e-9f) {
        return p1.y;
      }
      float slope = (p2.y - p1.y) / (p2.x - p1.x);
      return p1.y + (x - p1.x) * slope;
    }
  }
  
  // Fallback (shouldn't reach here)
  return x;
}

float CalibrationEngine::calibrate_dfrobot_orp_(float x) const {
  // DFRobot SEN0165-like formula:
  // ORP_mV = (mid_mv - (raw_voltage * 1000)) - offset_mv
  //
  // mid_mv: reference midpoint (typically 2500mV for 2.5V VRef)
  // raw_voltage: from ADC (0-5V range typically)
  // offset_mv: calibration offset from known ORP solution
  
  float raw_mv = x * 1000.0f;
  float orp_mv = (this->dfrobot_mid_mv_ - raw_mv) - this->dfrobot_offset_mv_;
  
  return orp_mv;
}

void CalibrationEngine::load_from_preferences() {
  if (this->prefs_key_ == 0) {
    ESP_LOGD(CAL_TAG, "No preferences key set, skipping load");
    return;
  }
  
  this->prefs_ = global_preferences->make_preference<CalibrationPrefsData>(this->prefs_key_);
  
  CalibrationPrefsData data;
  if (!this->prefs_.load(&data)) {
    ESP_LOGD(CAL_TAG, "No saved calibration preferences found");
    return;
  }
  
  // Accept current magic or legacy Lot 2 magic
  if (data.magic != CALIBRATION_PREFS_MAGIC && data.magic != CALIBRATION_PREFS_MAGIC_V2) {
    ESP_LOGD(CAL_TAG, "Invalid calibration preferences magic (0x%08X), ignoring", data.magic);
    return;
  }
  
  bool is_legacy_format = (data.magic == CALIBRATION_PREFS_MAGIC_V2);
  
  // Restore calibration type
  this->type_ = static_cast<CalibrationType>(data.calibration_type);
  
  // Restore DFRobot params
  this->dfrobot_mid_mv_ = data.dfrobot_mid_mv;
  this->dfrobot_offset_mv_ = data.dfrobot_offset_mv;
  
  // Restore calibration temperature (Lot 5)
  if (!is_legacy_format) {
    this->calibration_temperature_ = data.calibration_temperature;
  } else {
    this->calibration_temperature_ = NAN;  // Legacy format had no Tw
  }
  
  // Restore points to live set
  this->live_points_.clear();
  for (uint8_t i = 0; i < data.point_count && i < MAX_CALIBRATION_POINTS; i++) {
    CalibrationPoint pt;
    pt.x = data.points_x[i];
    pt.y = data.points_y[i];
    if (pt.is_valid()) {
      this->live_points_.push_back(pt);
    }
  }
  
  // Sort by x
  std::sort(this->live_points_.begin(), this->live_points_.end(), CalibrationPoint::compare_by_x);
  
  this->poly_coeffs_valid_ = false;
  
  if (!std::isnan(this->calibration_temperature_)) {
    ESP_LOGI(CAL_TAG, "Loaded calibration from preferences: type=%s, %zu points, Tw=%.1f°C",
             this->get_type_name(), this->live_points_.size(), this->calibration_temperature_);
  } else {
    ESP_LOGI(CAL_TAG, "Loaded calibration from preferences: type=%s, %zu points",
             this->get_type_name(), this->live_points_.size());
  }
}

void CalibrationEngine::save_to_preferences() {
  if (this->prefs_key_ == 0) {
    ESP_LOGW(CAL_TAG, "No preferences key set, cannot save");
    return;
  }
  
  CalibrationPrefsData data;
  data.magic = CALIBRATION_PREFS_MAGIC;
  data.calibration_type = static_cast<uint8_t>(this->type_);
  data.dfrobot_mid_mv = this->dfrobot_mid_mv_;
  data.dfrobot_offset_mv = this->dfrobot_offset_mv_;
  data.calibration_temperature = this->calibration_temperature_;  // Lot 5
  
  // Save live points (not draft)
  data.point_count = std::min((size_t)MAX_CALIBRATION_POINTS, this->live_points_.size());
  for (uint8_t i = 0; i < data.point_count; i++) {
    data.points_x[i] = this->live_points_[i].x;
    data.points_y[i] = this->live_points_[i].y;
  }
  
  // Zero out unused slots
  for (uint8_t i = data.point_count; i < MAX_CALIBRATION_POINTS; i++) {
    data.points_x[i] = NAN;
    data.points_y[i] = NAN;
  }
  
  this->prefs_.save(&data);
  
  if (!std::isnan(this->calibration_temperature_)) {
    ESP_LOGI(CAL_TAG, "Saved calibration to preferences: type=%s, %d points, Tw=%.1f°C",
             this->get_type_name(), data.point_count, this->calibration_temperature_);
  } else {
    ESP_LOGI(CAL_TAG, "Saved calibration to preferences: type=%s, %d points",
             this->get_type_name(), data.point_count);
  }
}

void CalibrationEngine::dump_config() const {
  ESP_LOGCONFIG(CAL_TAG, "  Calibration type: %s", this->get_type_name());
  ESP_LOGCONFIG(CAL_TAG, "  Calibration valid: %s", this->is_valid() ? "YES" : "NO");
  ESP_LOGCONFIG(CAL_TAG, "  Live point count: %zu (min required: %d)", 
                this->live_points_.size(), this->get_minimum_points());
  
  // Lot 7: Draft mode status
  if (this->draft_mode_enabled_) {
    ESP_LOGCONFIG(CAL_TAG, "  Draft mode: ENABLED (%zu draft points, %s)",
                  this->draft_points_.size(),
                  this->has_draft_changes_ ? "uncommitted changes" : "no changes");
  }
  
  if (this->type_ == CAL_TYPE_POLYNOMIAL) {
    ESP_LOGCONFIG(CAL_TAG, "  Polynomial order: %d", this->polynomial_order_);
  }
  
  if (this->type_ == CAL_TYPE_DFROBOT_ORP) {
    ESP_LOGCONFIG(CAL_TAG, "  DFRobot mid_mv: %.1f", this->dfrobot_mid_mv_);
    ESP_LOGCONFIG(CAL_TAG, "  DFRobot offset_mv: %.1f", this->dfrobot_offset_mv_);
  }
  
  // Lot 5: Show calibration temperature if available
  if (!std::isnan(this->calibration_temperature_)) {
    ESP_LOGCONFIG(CAL_TAG, "  Calibration Tw: %.1f °C", this->calibration_temperature_);
  }
  
  // Show live points
  for (size_t i = 0; i < this->live_points_.size(); i++) {
    const CalibrationPoint &pt = this->live_points_[i];
    ESP_LOGCONFIG(CAL_TAG, "  Point %zu: x=%.4f, y=%.4f", i, pt.x, pt.y);
  }
}

}  // namespace pool_station
}  // namespace esphome
