#pragma once

#include "hardware_mapper.hpp"
#include "genetic_algorithm.hpp"
#include "constraint_solver.hpp"
#include "module_registry.hpp"

#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace spartan7 {

// ─────────────────────────────────────────────
//  Хендл логічного модуля (повертається user-у)
// ─────────────────────────────────────────────
struct ModuleHandle {
    int         id       = -1;
    std::string name;
    ModuleType  type     = ModuleType::CUSTOM;
    int         data_width = 8;

    // «Порти» для з'єднань (індекси в списку з'єднань)
    struct Port { int module_id; std::string port_name; };
    Port output;
    std::vector<Port> inputs;
};

// ─────────────────────────────────────────────
//  З'єднання між модулями
// ─────────────────────────────────────────────
struct Connection {
    ModuleHandle::Port src;
    ModuleHandle::Port dst;
};

// ─────────────────────────────────────────────
//  Статус компіляції/прошивання
// ─────────────────────────────────────────────
enum class BuildStatus {
    NOT_STARTED,
    ANALYZING,
    OPTIMIZING,
    GENERATING_VHDL,
    SYNTHESIZING,
    PLACE_AND_ROUTE,
    GENERATING_BITSTREAM,
    PROGRAMMING,
    DONE,
    FAILED
};

struct BuildResult {
    BuildStatus status = BuildStatus::NOT_STARTED;
    std::string message;
    std::string bitstream_path;
    std::string report_path;
    double      total_time_s    = 0.0;
    ResourceUsage final_usage;
    GAStatistics  ga_stats;
};

// ─────────────────────────────────────────────
//  Головний публічний API
// ─────────────────────────────────────────────
class Spartan7Optimizer {
public:
    // device_id: "xc7s50-1fgg484", "xc7s25-1csga225", тощо
    explicit Spartan7Optimizer(const std::string& device_id,
                                const GAParameters& ga_params = {});
    ~Spartan7Optimizer();

    // ── Створення логічних модулів ───────────
    ModuleHandle createMUX  (int inputs,     int data_width = 1);
    ModuleHandle createDEMUX(int outputs,    int data_width = 1);
    ModuleHandle createBlockRAM(int size_bytes, int data_width = 8);
    ModuleHandle createFIFO (int depth,      int data_width = 8);
    ModuleHandle createALU  (int data_width, bool with_carry = true);
    ModuleHandle createMUL  (int a_width,    int b_width);
    ModuleHandle createDSP48E1(int a_width,  int b_width, int p_width = 48);

    // ── З'єднання ────────────────────────────
    void connect(const ModuleHandle::Port& src, const ModuleHandle::Port& dst);

    // ── Обмеження ────────────────────────────
    void setClockFrequency(double mhz);           // за замовч. 100 МГц
    void setAreaBudget(const AreaBudget& budget);
    void setPreferredRegion(const DeviceLocation& loc); // флорплан

    // ── Головний цикл: аналіз → ГА → VHDL ──
    // progress_cb: опціональний callback прогресу (0.0..1.0, рядок статусу)
    BuildResult compile(
        std::function<void(double, const std::string&)> progress_cb = nullptr);

    // ── Прошивання ────────────────────────────
    BuildResult program(const std::string& jtag_cable = "auto");

    // ── Звітність ────────────────────────────
    ResourceUsage getResourceUsage()    const;
    std::string   getOptimizationReport() const;
    std::string   getTimingReport()       const;

    // ── Контроль ────────────────────────────
    void reset();   // очистити всі модулі та з'єднання

private:
    struct Impl;                        // PIMPL — приховує залежності від Vivado/Python
    std::unique_ptr<Impl> impl_;
};

} // namespace spartan7
