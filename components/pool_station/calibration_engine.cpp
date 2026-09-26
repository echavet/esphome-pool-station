#include "calibration_engine.h"

namespace esphome {
namespace pool_station {

const char *CalibrationEngine::get_type_name() const {
  // Published / committed type — draft algorithm stays on get_type().
  return calibration_type_name(this->live_type_);
}

// ============================================================================
// Configuration (Lot 7: with validation callback)
// ============================================================================

void CalibrationEngine::set_type(CalibrationType type) {
  this->type_ = type;
  this->live_type_ = type;
  this->invalidate_fit_caches_();
}

void CalibrationEngine::set_type_runtime(CalibrationType type) {
  if (this->type_ == type) {
    return;
  }
  
  CalibrationType old_type = this->type_;
  this->type_ = type;
  if (this->draft_mode_enabled_) {
    this->mark_draft_dirty_();
  } else {
    this->live_type_ = type;
    this->invalidate_fit_caches_();
  }
  
  ESP_LOGI(CAL_TAG, "Calibration algorithm changed: %s -> %s%s", 
           calibration_type_name(old_type), calibration_type_name(type),
           this->draft_mode_enabled_ ? " [DRAFT]" : "");
  
  // Revalidate published state (live) and notify
  this->notify_validation_changed_();
}

void CalibrationEngine::set_dfrobot_mid_mv(float mid) {
  this->dfrobot_mid_mv_ = mid;
  if (this->draft_mode_enabled_) {
    this->mark_draft_dirty_();
  } else {
    this->live_dfrobot_mid_mv_ = mid;
  }
}

void CalibrationEngine::set_dfrobot_offset_mv(float offset) {
  this->dfrobot_offset_mv_ = offset;
  if (this->draft_mode_enabled_) {
    this->mark_draft_dirty_();
  } else {
    this->live_dfrobot_offset_mv_ = offset;
  }
}

void CalibrationEngine::notify_validation_changed_() {
  if (this->validation_callback_) {
    bool valid = this->is_valid();
    this->validation_callback_(valid);
  }
}

void CalibrationEngine::mark_draft_dirty_() {
  if (this->draft_mode_enabled_) {
    this->has_draft_changes_ = true;
  }
}

void CalibrationEngine::commit_working_params_to_live_() {
  this->live_type_ = this->type_;
  this->live_dfrobot_mid_mv_ = this->dfrobot_mid_mv_;
  this->live_dfrobot_offset_mv_ = this->dfrobot_offset_mv_;
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
  this->invalidate_fit_caches_();
  
  // Do not sort: HA Capturer entities map to slot indices.
  
  this->mark_draft_dirty_();
  
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
  this->invalidate_fit_caches_();
  
  // Do not re-sort: keep HA slot indices stable.
  
  this->mark_draft_dirty_();
  
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
  this->invalidate_fit_caches_();
  
  this->mark_draft_dirty_();
  
  ESP_LOGD(CAL_TAG, "Removed calibration point %zu (remaining: %zu)%s", 
           index, points.size(), this->draft_mode_enabled_ ? " [DRAFT]" : "");
  
  this->notify_validation_changed_();
}

void CalibrationEngine::clear_points() {
  auto &points = this->points_();
  points.clear();
  this->invalidate_fit_caches_();
  
  this->mark_draft_dirty_();
  
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
  // Seed points go to live set in YAML/slot order (do not sort).
  this->live_points_ = points;
  this->invalidate_fit_caches_();
  
  if (this->draft_mode_enabled_) {
    this->sync_draft_from_live();
  }
  
  ESP_LOGD(CAL_TAG, "Set %zu seed calibration points", this->live_points_.size());
}

// ============================================================================
// Draft/Commit workflow (Lot 7)
// ============================================================================

void CalibrationEngine::sync_draft_from_live() {
  this->draft_points_ = this->live_points_;
  this->type_ = this->live_type_;
  this->dfrobot_mid_mv_ = this->live_dfrobot_mid_mv_;
  this->dfrobot_offset_mv_ = this->live_dfrobot_offset_mv_;
  this->has_draft_changes_ = false;
}

void CalibrationEngine::enable_draft_mode() {
  if (this->draft_mode_enabled_) {
    return;
  }
  
  // Snapshot committed params, then copy live → draft
  this->live_type_ = this->type_;
  this->live_dfrobot_mid_mv_ = this->dfrobot_mid_mv_;
  this->live_dfrobot_offset_mv_ = this->dfrobot_offset_mv_;
  this->draft_mode_enabled_ = true;
  this->sync_draft_from_live();
  
  ESP_LOGI(CAL_TAG, "Draft mode ENABLED (copied %zu live points to draft)", this->live_points_.size());
}

void CalibrationEngine::disable_draft_mode() {
  if (!this->draft_mode_enabled_) {
    return;
  }
  
  if (this->has_draft_changes_) {
    ESP_LOGW(CAL_TAG, "Draft mode DISABLED with uncommitted changes (discarded)");
  } else {
    ESP_LOGI(CAL_TAG, "Draft mode DISABLED");
  }
  
  this->sync_draft_from_live();
  this->draft_mode_enabled_ = false;
  this->draft_points_.clear();
}

void CalibrationEngine::commit_draft() {
  if (!this->draft_mode_enabled_) {
    ESP_LOGW(CAL_TAG, "Cannot commit: draft mode not enabled");
    return;
  }

  if (!this->is_draft_valid()) {
    ESP_LOGW(CAL_TAG,
             "Cannot commit: draft calibration is invalid (type=%s, %zu points, min=%d) — live unchanged",
             calibration_type_name(this->type_), this->draft_points_.size(),
             this->get_minimum_points_for_type(this->type_));
    return;
  }
  
  // Copy draft points + working algo/params to live
  this->live_points_ = this->draft_points_;
  this->commit_working_params_to_live_();
  this->has_draft_changes_ = false;
  this->invalidate_fit_caches_();
  
  ESP_LOGI(CAL_TAG, "Committed %zu draft points to live calibration (type=%s)",
           this->live_points_.size(), calibration_type_name(this->live_type_));
  
  // Save to preferences
  this->save_to_preferences();
  
  this->notify_validation_changed_();
}

void CalibrationEngine::discard_draft() {
  if (!this->draft_mode_enabled_) {
    ESP_LOGW(CAL_TAG, "Cannot discard: draft mode not enabled");
    return;
  }
  
  this->sync_draft_from_live();
  
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
  return this->get_minimum_points_for_type(this->live_type_);
}

bool CalibrationEngine::is_type_implemented(CalibrationType type) {
  switch (type) {
    case CAL_TYPE_NONE:
    case CAL_TYPE_LINEAR:
    case CAL_TYPE_POLYNOMIAL:
    case CAL_TYPE_PIECEWISE:
    case CAL_TYPE_DFROBOT_ORP:
      return true;
    case CAL_TYPE_EXPONENTIAL:
    case CAL_TYPE_LOGARITHMIC:
    case CAL_TYPE_POWER:
    default:
      return false;
  }
}

bool CalibrationEngine::is_points_valid_(const std::vector<CalibrationPoint> &points,
                                         CalibrationType type) const {
  if (!is_type_implemented(type)) {
    return false;
  }

  if (type == CAL_TYPE_NONE) {
    return false;
  }

  if (type == CAL_TYPE_DFROBOT_ORP) {
    return true;
  }

  uint8_t min_pts = this->get_minimum_points_for_type(type);
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

bool CalibrationEngine::is_valid() const {
  // Published validity: last committed type + live points
  return this->is_points_valid_(this->live_points_, this->live_type_);
}

bool CalibrationEngine::is_draft_valid() const {
  // Working set (draft when draft_mode is on) — for UI feedback
  return this->is_points_valid_(this->points_(), this->type_);
}

float CalibrationEngine::apply_precision_(float value) const {
  if (std::isnan(value)) {
    return value;
  }
  float scale = std::pow(10.0f, static_cast<float>(this->precision_decimals_));
  return std::round(value * scale) / scale;
}

std::vector<CalibrationPoint> CalibrationEngine::valid_points_copy_(
    const std::vector<CalibrationPoint> &src, bool sort_by_x) const {
  std::vector<CalibrationPoint> out;
  out.reserve(src.size());
  for (const auto &pt : src) {
    if (pt.is_valid()) {
      out.push_back(pt);
    }
  }
  if (sort_by_x) {
    std::sort(out.begin(), out.end(), CalibrationPoint::compare_by_x);
  }
  return out;
}

float CalibrationEngine::calibrate(float raw_voltage) const {
  if (std::isnan(raw_voltage)) {
    return NAN;
  }

  float result = raw_voltage;
  // Published output always uses the last committed algorithm + live points.
  switch (this->live_type_) {
    case CAL_TYPE_NONE:
      result = raw_voltage;  // Pass-through
      break;
      
    case CAL_TYPE_LINEAR:
      result = this->calibrate_linear_(raw_voltage);
      break;
      
    case CAL_TYPE_POLYNOMIAL:
      result = this->calibrate_polynomial_(raw_voltage);
      break;
      
    case CAL_TYPE_PIECEWISE:
      result = this->calibrate_piecewise_(raw_voltage);
      break;
      
    case CAL_TYPE_DFROBOT_ORP:
      result = this->calibrate_dfrobot_orp_(raw_voltage);
      break;
      
    case CAL_TYPE_EXPONENTIAL:
    case CAL_TYPE_LOGARITHMIC:
    case CAL_TYPE_POWER:
      // TODO: implement these algorithms
      ESP_LOGW(CAL_TAG, "Calibration type '%s' not yet implemented, using pass-through",
               this->get_type_name());
      result = raw_voltage;
      break;
      
    default:
      result = raw_voltage;
      break;
  }
  return this->apply_precision_(result);
}

void CalibrationEngine::invalidate_fit_caches_() const {
  this->poly_coeffs_valid_ = false;
  this->linear_cache_valid_ = false;
  this->linear_cache_ok_ = false;
  this->piecewise_cache_valid_ = false;
}

void CalibrationEngine::ensure_linear_cache_() const {
  if (this->linear_cache_valid_) {
    return;
  }
  this->linear_cache_valid_ = true;
  this->linear_cache_ok_ = false;
  this->linear_slope_ = 0.0f;
  this->linear_intercept_ = 0.0f;

  // Least-squares fit on all valid live points (N==2 equals the two-point line).
  // Do not mutate live_points_ — HA slots stay in capture/YAML order.
  auto pts = this->valid_points_copy_(this->live_points_, false);
  if (pts.size() < 2) {
    ESP_LOGW(CAL_TAG, "Linear calibration needs 2 points, have %zu", pts.size());
    return;
  }

  double n = static_cast<double>(pts.size());
  double sum_x = 0.0;
  double sum_y = 0.0;
  double sum_xx = 0.0;
  double sum_xy = 0.0;
  for (const auto &pt : pts) {
    double xi = pt.x;
    double yi = pt.y;
    sum_x += xi;
    sum_y += yi;
    sum_xx += xi * xi;
    sum_xy += xi * yi;
  }

  double denom = n * sum_xx - sum_x * sum_x;
  if (std::abs(denom) < 1e-12) {
    ESP_LOGW(CAL_TAG, "Linear calibration: points have same x value");
    this->linear_slope_ = 0.0f;
    this->linear_intercept_ = static_cast<float>(sum_y / n);
    this->linear_cache_ok_ = true;
    return;
  }

  double slope = (n * sum_xy - sum_x * sum_y) / denom;
  double intercept = (sum_y - slope * sum_x) / n;
  this->linear_slope_ = static_cast<float>(slope);
  this->linear_intercept_ = static_cast<float>(intercept);
  this->linear_cache_ok_ = true;
}

float CalibrationEngine::calibrate_linear_(float x) const {
  this->ensure_linear_cache_();
  if (!this->linear_cache_ok_) {
    return x;
  }
  return this->linear_intercept_ + this->linear_slope_ * x;
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
  this->invalidate_fit_caches_();
  
  // Always use live points for actual calibration (skip invalid slots)
  auto pts = this->valid_points_copy_(this->live_points_, false);
  size_t n = pts.size();
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
        float xi = pts[k].x;
        sum += std::pow(xi, (float)(i + j));
      }
      matrix[i][j] = sum;
    }
    
    // X^T * y
    float sum = 0.0f;
    for (size_t k = 0; k < n; k++) {
      float xi = pts[k].x;
      float yi = pts[k].y;
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

void CalibrationEngine::ensure_piecewise_cache_() const {
  if (this->piecewise_cache_valid_) {
    return;
  }
  // Sort a working copy only — stored slot indices stay YAML/HA stable.
  this->piecewise_pts_ = this->valid_points_copy_(this->live_points_, true);
  this->piecewise_cache_valid_ = true;
}

float CalibrationEngine::calibrate_piecewise_(float x) const {
  this->ensure_piecewise_cache_();
  const auto &pts = this->piecewise_pts_;
  if (pts.size() < 2) {
    ESP_LOGW(CAL_TAG, "Piecewise calibration needs at least 2 points, have %zu", pts.size());
    return x;
  }

  // Below first point: extrapolate from first segment
  if (x <= pts.front().x) {
    const CalibrationPoint &p1 = pts[0];
    const CalibrationPoint &p2 = pts[1];
    float slope = (p2.y - p1.y) / (p2.x - p1.x);
    return p1.y + (x - p1.x) * slope;
  }

  // Above last point: extrapolate from last segment
  if (x >= pts.back().x) {
    size_t n = pts.size();
    const CalibrationPoint &p1 = pts[n - 2];
    const CalibrationPoint &p2 = pts[n - 1];
    float slope = (p2.y - p1.y) / (p2.x - p1.x);
    return p2.y + (x - p2.x) * slope;
  }

  // Find segment [i, i+1] where pts[i].x <= x <= pts[i+1].x
  for (size_t i = 0; i < pts.size() - 1; i++) {
    const CalibrationPoint &p1 = pts[i];
    const CalibrationPoint &p2 = pts[i + 1];
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
  float orp_mv = (this->live_dfrobot_mid_mv_ - raw_mv) - this->live_dfrobot_offset_mv_;
  
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
  
  // Restore calibration type to both live and working
  this->type_ = static_cast<CalibrationType>(data.calibration_type);
  this->live_type_ = this->type_;
  
  // Restore DFRobot params
  this->dfrobot_mid_mv_ = data.dfrobot_mid_mv;
  this->dfrobot_offset_mv_ = data.dfrobot_offset_mv;
  this->live_dfrobot_mid_mv_ = this->dfrobot_mid_mv_;
  this->live_dfrobot_offset_mv_ = this->dfrobot_offset_mv_;
  
  // Restore calibration temperature (Lot 5)
  if (!is_legacy_format) {
    this->calibration_temperature_ = data.calibration_temperature;
  } else {
    this->calibration_temperature_ = NAN;  // Legacy format had no Tw
  }
  
  // Restore points to live set in saved slot order (including NaN holes).
  // Do not sort or compact: HA Capturer indices must survive reboot.
  this->live_points_.clear();
  for (uint8_t i = 0; i < data.point_count && i < MAX_CALIBRATION_POINTS; i++) {
    CalibrationPoint pt;
    pt.x = data.points_x[i];
    pt.y = data.points_y[i];
    this->live_points_.push_back(pt);
  }
  
  this->invalidate_fit_caches_();

  // Prefs load happens after codegen enable_draft_mode() — refresh draft so
  // UI numbers match committed flash, not YAML seeds. Browser refresh is not undo.
  if (this->draft_mode_enabled_) {
    this->sync_draft_from_live();
  }
  
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
  data.calibration_type = static_cast<uint8_t>(this->live_type_);
  data.dfrobot_mid_mv = this->live_dfrobot_mid_mv_;
  data.dfrobot_offset_mv = this->live_dfrobot_offset_mv_;
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
  ESP_LOGCONFIG(CAL_TAG, "  Calibration type (live): %s", calibration_type_name(this->live_type_));
  ESP_LOGCONFIG(CAL_TAG, "  Calibration valid: %s", this->is_valid() ? "YES" : "NO");
  ESP_LOGCONFIG(CAL_TAG, "  Live point count: %zu (min required: %d)", 
                this->live_points_.size(), this->get_minimum_points_for_type(this->live_type_));
  
  // Lot 7: Draft mode status
  if (this->draft_mode_enabled_) {
    ESP_LOGCONFIG(CAL_TAG, "  Draft mode: ENABLED (%zu draft points, type=%s, %s)",
                  this->draft_points_.size(),
                  calibration_type_name(this->type_),
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
