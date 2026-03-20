#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <unordered_map>
#include <optional>

namespace spartan7 {

// ─────────────────────────────────────────────
//  Типи ресурсів Spartan-7
// ─────────────────────────────────────────────
enum class ResourceType {
    LUT,        // Look-Up Table (6-input)
    FF,         // Flip-Flop
    BRAM,       // Block RAM 36Kb
    DSP,        // DSP48E1 slice
    SRL,        // Shift Register LUT
    CARRY,      // CARRY4 chain
    IO,         // I/O buffer
    UNKNOWN
};

enum class ImplementationStyle {
    LUT_BASED,      // Реалізація на LUT-ах
    SRL_BASED,      // Реалізація на зсувних регістрах
    BRAM_BASED,     // Реалізація на блочній пам'яті
    DSP_BASED,      // Реалізація на DSP48E1
    AUTO            // Оптимальний вибір автоматично
};

// ─────────────────────────────────────────────
//  Фізичне місцезнаходження на FPGA
// ─────────────────────────────────────────────
struct DeviceLocation {
    int   slice_x   = -1;
    int   slice_y   = -1;
    int   bel_index = -1;   // Basic Element of Logic (0-7 в LUT slice)
    std::string site_name;  // напр. "SLICE_X12Y34"
};

// ─────────────────────────────────────────────
//  Дескриптор одного фізичного ресурсу
// ─────────────────────────────────────────────
struct HardwareResource {
    int              id         = -1;
    ResourceType     type       = ResourceType::UNKNOWN;
    DeviceLocation   location;
    int              capacity   = 0;    // біт/клітин залежно від типу
    bool             available  = true;
    double           power_mw   = 0.0; // мВт при 100% активності
    double           delay_ns   = 0.0; // внутрішня затримка

    std::string toString() const;
};

// ─────────────────────────────────────────────
//  Обмеження маппінгу
// ─────────────────────────────────────────────
struct MappingConstraint {
    double   max_delay_ns     = 10.0;   // при тактовій 100 МГц
    int      max_lut_count    = 0;      // 0 = необмежено
    int      max_bram_count   = 0;
    int      max_dsp_count    = 0;
    bool     require_pipelining = false;
    std::optional<DeviceLocation> preferred_region; // бажаний регіон
};

// ─────────────────────────────────────────────
//  Результат оптимального маппінгу
// ─────────────────────────────────────────────
struct OptimalMapping {
    std::vector<HardwareResource> resources;
    ImplementationStyle           style         = ImplementationStyle::AUTO;
    double                        timing_ns     = 0.0;
    double                        power_mw      = 0.0;
    int                           lut_count     = 0;
    int                           ff_count      = 0;
    int                           bram_count    = 0;
    int                           dsp_count     = 0;
    double                        fitness_score = 0.0; // GA fitness
    bool                          is_valid      = false;

    std::string toReport() const;
};

// ─────────────────────────────────────────────
//  Поточне використання ресурсів
// ─────────────────────────────────────────────
struct ResourceUsage {
    int lut_used    = 0;  int lut_total    = 0;
    int ff_used     = 0;  int ff_total     = 0;
    int bram_used   = 0;  int bram_total   = 0;  // у одиницях 36Kb
    int dsp_used    = 0;  int dsp_total    = 0;
    int io_used     = 0;  int io_total     = 0;

    double lut_utilization()  const { return lut_total  ? 100.0 * lut_used  / lut_total  : 0; }
    double bram_utilization() const { return bram_total ? 100.0 * bram_used / bram_total : 0; }
    double dsp_utilization()  const { return dsp_total  ? 100.0 * dsp_used  / dsp_total  : 0; }
};

// ─────────────────────────────────────────────
//  Клас маппера
// ─────────────────────────────────────────────
class HardwareMapper {
public:
    explicit HardwareMapper(const std::string& device_id);

    // Маппінг конкретних модулів
    OptimalMapping mapMuxToSlice(int inputs, int data_width,
                                  const MappingConstraint& constraint = {});
    OptimalMapping mapDemuxToSlice(int outputs, int data_width,
                                    const MappingConstraint& constraint = {});
    OptimalMapping mapMemoryBlock(int size_bits, int data_width,
                                   const MappingConstraint& constraint = {});
    OptimalMapping mapFIFO(int depth, int data_width,
                            const MappingConstraint& constraint = {});
    OptimalMapping mapDSP(int a_width, int b_width,
                           const MappingConstraint& constraint = {});
    OptimalMapping mapALU(int data_width, bool with_carry,
                           const MappingConstraint& constraint = {});

    // Аналіз затримки ланцюжка модулів
    double calculatePathLatency(const std::vector<OptimalMapping>& chain) const;

    // Поточне використання ресурсів
    ResourceUsage getResourceUsage() const;

    // Перевірка чи вміщаються всі поточні маппінги в пристрій
    bool checkFit() const;

    // Скидання всіх зарезервованих ресурсів
    void reset();

private:
    std::string device_id_;
    ResourceUsage usage_;
    std::vector<OptimalMapping> allocated_mappings_;

    // Кількість LUT необхідних для MUX(N:1) шириною W
    static int estimateMuxLUTs(int inputs, int width);
    // Вибір оптимального стилю залежно від обмежень
    ImplementationStyle chooseStyle(int inputs, int depth,
                                     const MappingConstraint& constraint) const;
    // Заповнення параметрів ресурсу Spartan-7 на основі типу пристрою
    void loadDeviceParameters();

    // Spartan-7 xc7s50 параметри (за замовчуванням)
    int device_lut_total_   = 32600;
    int device_ff_total_    = 65200;
    int device_bram_total_  = 75;   // 36Kb блоків
    int device_dsp_total_   = 120;
    int device_io_total_    = 250;
};

} // namespace spartan7
