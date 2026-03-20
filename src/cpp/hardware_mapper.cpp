#include "hardware_mapper.hpp"
#include <stdexcept>
#include <sstream>
#include <algorithm>
#include <cmath>

namespace spartan7 {

// ─────────────────────────────────────────
//  HardwareResource helpers
// ─────────────────────────────────────────
std::string HardwareResource::toString() const {
    std::ostringstream ss;
    const char* type_names[] = {"LUT","FF","BRAM","DSP","SRL","CARRY","IO","UNKNOWN"};
    ss << type_names[static_cast<int>(type)]
       << " @ " << location.site_name
       << " cap=" << capacity
       << " avail=" << (available ? "yes" : "no")
       << " delay=" << delay_ns << "ns";
    return ss.str();
}

// ─────────────────────────────────────────
//  OptimalMapping helpers
// ─────────────────────────────────────────
std::string OptimalMapping::toReport() const {
    std::ostringstream ss;
    ss << "=== Mapping Report ===\n"
       << "  Valid:    " << (is_valid ? "YES" : "NO") << "\n"
       << "  Style:    " << static_cast<int>(style) << "\n"
       << "  Timing:   " << timing_ns   << " ns\n"
       << "  Power:    " << power_mw    << " mW\n"
       << "  LUTs:     " << lut_count   << "\n"
       << "  FFs:      " << ff_count    << "\n"
       << "  BRAMs:    " << bram_count  << "\n"
       << "  DSPs:     " << dsp_count   << "\n"
       << "  Fitness:  " << fitness_score << "\n";
    return ss.str();
}

// ─────────────────────────────────────────
//  HardwareMapper
// ─────────────────────────────────────────
HardwareMapper::HardwareMapper(const std::string& device_id)
    : device_id_(device_id) {
    loadDeviceParameters();
}

void HardwareMapper::loadDeviceParameters() {
    // Spartan-7 product family — задаємо ресурси за part number
    if (device_id_.find("xc7s50") != std::string::npos) {
        device_lut_total_  = 32600;
        device_ff_total_   = 65200;
        device_bram_total_ = 75;
        device_dsp_total_  = 120;
        device_io_total_   = 250;
    } else if (device_id_.find("xc7s25") != std::string::npos) {
        device_lut_total_  = 14600;
        device_ff_total_   = 29200;
        device_bram_total_ = 45;
        device_dsp_total_  = 80;
        device_io_total_   = 150;
    } else if (device_id_.find("xc7s15") != std::string::npos) {
        device_lut_total_  = 8000;
        device_ff_total_   = 16000;
        device_bram_total_ = 20;
        device_dsp_total_  = 40;
        device_io_total_   = 100;
    }
    // Якщо невідомий пристрій — залишаємо значення за замовч.
}

int HardwareMapper::estimateMuxLUTs(int inputs, int width) {
    // Кожен 6-input LUT може реалізувати MUX(4:1) для 1 біта.
    // MUX(N:1) потребує ceil(log2(N)) рівнів.
    int levels = 0;
    int n = inputs;
    while (n > 1) { n = (n + 3) / 4; levels++; }
    // Грубота: 1 LUT на 1 вихідний біт на рівень
    return std::max(1, levels) * width;
}

ImplementationStyle HardwareMapper::chooseStyle(int inputs, int depth,
                                                  const MappingConstraint& c) const {
    if (c.max_delay_ns < 3.0) {
        return ImplementationStyle::LUT_BASED;   // найшвидший
    }
    if (depth > 64) {
        return ImplementationStyle::BRAM_BASED;  // глибока пам'ять
    }
    if (inputs <= 32) {
        return ImplementationStyle::SRL_BASED;   // рядки до 32 — SRL16/32
    }
    return ImplementationStyle::LUT_BASED;
}

OptimalMapping HardwareMapper::mapMuxToSlice(int inputs, int data_width,
                                               const MappingConstraint& constraint) {
    OptimalMapping m;
    m.style     = chooseStyle(inputs, 0, constraint);
    m.lut_count = estimateMuxLUTs(inputs, data_width);
    m.ff_count  = 0; // комбінаційна логіка
    m.timing_ns = 0.5 + 0.3 * std::ceil(std::log2(std::max(inputs, 2)));
    m.power_mw  = 0.1 * m.lut_count;
    m.is_valid  = (usage_.lut_used + m.lut_count) <= device_lut_total_;

    if (m.is_valid) {
        usage_.lut_used += m.lut_count;
        allocated_mappings_.push_back(m);
    }
    return m;
}

OptimalMapping HardwareMapper::mapDemuxToSlice(int outputs, int data_width,
                                                 const MappingConstraint& constraint) {
    // DEMUX ~ ті ж витрати що й MUX симетрично
    return mapMuxToSlice(outputs, data_width, constraint);
}

OptimalMapping HardwareMapper::mapMemoryBlock(int size_bits, int data_width,
                                               const MappingConstraint& constraint) {
    OptimalMapping m;
    const int bram_size_bits = 36 * 1024;

    if (size_bits > 512 || constraint.max_bram_count > 0) {
        // BRAM реалізація
        m.style      = ImplementationStyle::BRAM_BASED;
        m.bram_count = std::max(1, (size_bits + bram_size_bits - 1) / bram_size_bits);
        m.lut_count  = 4;  // контролер/декодер адреси
        m.ff_count   = 8;
        m.timing_ns  = 2.0; // BRAM read latency
        m.power_mw   = 5.0 * m.bram_count;
    } else {
        // Distributed RAM
        m.style      = ImplementationStyle::LUT_BASED;
        m.lut_count  = std::max(1, (size_bits + 5) / 6);
        m.timing_ns  = 1.5;
        m.power_mw   = 0.12 * m.lut_count;
    }

    m.is_valid = (usage_.bram_used + m.bram_count) <= device_bram_total_
              && (usage_.lut_used  + m.lut_count)  <= device_lut_total_;
    if (m.is_valid) {
        usage_.bram_used += m.bram_count;
        usage_.lut_used  += m.lut_count;
        usage_.ff_used   += m.ff_count;
        allocated_mappings_.push_back(m);
    }
    return m;
}

OptimalMapping HardwareMapper::mapFIFO(int depth, int data_width,
                                        const MappingConstraint& constraint) {
    OptimalMapping m;
    int total_bits = depth * data_width;
    m = mapMemoryBlock(total_bits, data_width, constraint);
    // FIFO додатково потребує указівники та логіку full/empty
    m.lut_count += 12;
    m.ff_count  += 16;
    m.timing_ns  = std::max(m.timing_ns, 2.5);
    return m;
}

OptimalMapping HardwareMapper::mapDSP(int a_width, int b_width,
                                       const MappingConstraint& constraint) {
    OptimalMapping m;
    m.style      = ImplementationStyle::DSP_BASED;
    m.dsp_count  = 1;
    m.lut_count  = 2;   // pipeline regs / bypass
    m.ff_count   = 4;
    m.timing_ns  = 3.8; // DSP48E1 pipeline latency
    m.power_mw   = 8.0;
    m.is_valid   = (usage_.dsp_used + m.dsp_count) <= device_dsp_total_;
    if (m.is_valid) {
        usage_.dsp_used += m.dsp_count;
        usage_.lut_used += m.lut_count;
        allocated_mappings_.push_back(m);
    }
    return m;
}

OptimalMapping HardwareMapper::mapALU(int data_width, bool with_carry,
                                       const MappingConstraint& constraint) {
    OptimalMapping m;
    m.style      = ImplementationStyle::LUT_BASED;
    m.lut_count  = data_width;      // 1 LUT на 1 біт
    m.ff_count   = with_carry ? data_width : 0;
    m.timing_ns  = with_carry ? (0.2 * data_width) : 0.5;
    m.power_mw   = 0.08 * data_width;
    m.is_valid   = (usage_.lut_used + m.lut_count) <= device_lut_total_;
    if (m.is_valid) {
        usage_.lut_used += m.lut_count;
        usage_.ff_used  += m.ff_count;
        allocated_mappings_.push_back(m);
    }
    return m;
}

double HardwareMapper::calculatePathLatency(
        const std::vector<OptimalMapping>& chain) const {
    double total = 0.0;
    for (const auto& m : chain) {
        total += m.timing_ns;
        total += 0.15; // типова затримка routing між сусідніми елементами
    }
    return total;
}

ResourceUsage HardwareMapper::getResourceUsage() const {
    ResourceUsage ru = usage_;
    ru.lut_total   = device_lut_total_;
    ru.ff_total    = device_ff_total_;
    ru.bram_total  = device_bram_total_;
    ru.dsp_total   = device_dsp_total_;
    ru.io_total    = device_io_total_;
    return ru;
}

bool HardwareMapper::checkFit() const {
    return usage_.lut_used  <= device_lut_total_
        && usage_.ff_used   <= device_ff_total_
        && usage_.bram_used <= device_bram_total_
        && usage_.dsp_used  <= device_dsp_total_;
}

void HardwareMapper::reset() {
    usage_ = {};
    allocated_mappings_.clear();
}

} // namespace spartan7
