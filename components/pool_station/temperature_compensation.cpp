#include "temperature_compensation.h"

namespace esphome {
namespace pool_station {

// Implementation notes:
// 
// This module is mostly header-only since the compensation functions are
// simple inline calculations. The .cpp file exists for:
// - Future extension points
// - Explicit template instantiation if needed
// - Keeping the build consistent with other modules

// Nernst equation constants (for documentation)
// R = 8.314472 J/(mol·K)  — Gas constant
// F = 96485.3399 C/mol    — Faraday constant
// At 25°C: slope = 2.303 × R × T / F = 59.16 mV/pH
// 
// The ratio (T_ref + 273.15) / (T + 273.15) effectively compensates the
// temperature-dependent slope without needing the full Nernst constants.

}  // namespace pool_station
}  // namespace esphome
