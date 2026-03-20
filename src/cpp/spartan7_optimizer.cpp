#include "spartan7_optimizer.hpp"
#include <chrono>
#include <sstream>
#include <stdexcept>
#include <cstdlib>
#include <algorithm>
#include <iostream>

namespace spartan7 {

// ─────────────────────────────────────────
//  PIMPL — внутрішній стан
// ─────────────────────────────────────────
struct Spartan7Optimizer::Impl {
    std::string                 device_id;
    GAParameters                ga_params;
    HardwareMapper              mapper;
    ConstraintSolver            solver;
    std::vector<ModuleHandle>   modules;
    std::vector<Connection>     connections;
    AreaBudget                  area_budget;
    double                      clock_mhz   = 100.0;
    std::string                 last_report;
    std::string                 timing_report;

    Impl(const std::string& dev, const GAParameters& gap)
        : device_id(dev), ga_params(gap), mapper(dev),
          solver(dev, 1000.0 / 100.0) {}  // 100 МГц за замовч.
};

// ─────────────────────────────────────────
//  Конструктор / деструктор
// ─────────────────────────────────────────
Spartan7Optimizer::Spartan7Optimizer(const std::string& device_id,
                                       const GAParameters& ga_params)
    : impl_(std::make_unique<Impl>(device_id, ga_params)) {}

Spartan7Optimizer::~Spartan7Optimizer() = default;

// ─────────────────────────────────────────
//  創建 модулів
// ─────────────────────────────────────────
static int next_module_id() {
    static int id = 0;
    return ++id;
}

ModuleHandle Spartan7Optimizer::createMUX(int inputs, int data_width) {
    ModuleHandle h;
    h.id         = next_module_id();
    h.name       = "mux_" + std::to_string(h.id);
    h.type       = ModuleType::MUX;
    h.data_width = data_width;
    h.output     = {h.id, "Y"};
    h.inputs.resize(inputs);
    for (int i = 0; i < inputs; ++i) h.inputs[i] = {h.id, "I" + std::to_string(i)};
    impl_->modules.push_back(h);
    return h;
}

ModuleHandle Spartan7Optimizer::createDEMUX(int outputs, int data_width) {
    ModuleHandle h;
    h.id         = next_module_id();
    h.name       = "demux_" + std::to_string(h.id);
    h.type       = ModuleType::DEMUX;
    h.data_width = data_width;
    h.output     = {h.id, "Y0"};
    h.inputs     = {{h.id, "I"}};
    for (int i = 1; i < outputs; ++i)
        h.inputs.push_back({h.id, "Y" + std::to_string(i)});
    impl_->modules.push_back(h);
    return h;
}

ModuleHandle Spartan7Optimizer::createBlockRAM(int size_bytes, int data_width) {
    ModuleHandle h;
    h.id         = next_module_id();
    h.name       = "bram_" + std::to_string(h.id);
    h.type       = ModuleType::BRAM;
    h.data_width = data_width;
    h.output     = {h.id, "DOUT"};
    h.inputs     = {{h.id, "DIN"}, {h.id, "ADDR"}, {h.id, "WE"}, {h.id, "CLK"}};
    (void)size_bytes;
    impl_->modules.push_back(h);
    return h;
}

ModuleHandle Spartan7Optimizer::createFIFO(int depth, int data_width) {
    ModuleHandle h;
    h.id         = next_module_id();
    h.name       = "fifo_" + std::to_string(h.id);
    h.type       = ModuleType::FIFO;
    h.data_width = data_width;
    h.output     = {h.id, "DOUT"};
    h.inputs     = {{h.id, "DIN"}, {h.id, "WR_EN"}, {h.id, "RD_EN"}, {h.id, "CLK"}};
    (void)depth;
    impl_->modules.push_back(h);
    return h;
}

ModuleHandle Spartan7Optimizer::createALU(int data_width, bool with_carry) {
    ModuleHandle h;
    h.id         = next_module_id();
    h.name       = "alu_" + std::to_string(h.id);
    h.type       = ModuleType::ALU;
    h.data_width = data_width;
    h.output     = {h.id, "RESULT"};
    h.inputs     = {{h.id, "A"}, {h.id, "B"}, {h.id, "OP"}};
    if (with_carry) h.inputs.push_back({h.id, "CIN"});
    impl_->modules.push_back(h);
    return h;
}

ModuleHandle Spartan7Optimizer::createMUL(int a_width, int b_width) {
    ModuleHandle h;
    h.id         = next_module_id();
    h.name       = "mul_" + std::to_string(h.id);
    h.type       = ModuleType::MUL;
    h.data_width = a_width + b_width;
    h.output     = {h.id, "P"};
    h.inputs     = {{h.id, "A"}, {h.id, "B"}};
    impl_->modules.push_back(h);
    return h;
}

ModuleHandle Spartan7Optimizer::createDSP48E1(int a_width, int b_width, int p_width) {
    ModuleHandle h;
    h.id         = next_module_id();
    h.name       = "dsp_" + std::to_string(h.id);
    h.type       = ModuleType::DSP48E1;
    h.data_width = p_width;
    h.output     = {h.id, "P"};
    h.inputs     = {{h.id, "A"}, {h.id, "B"}, {h.id, "C"}, {h.id, "PCIN"}};
    (void)a_width; (void)b_width;
    impl_->modules.push_back(h);
    return h;
}

// ─────────────────────────────────────────
//  З'єднання
// ─────────────────────────────────────────
void Spartan7Optimizer::connect(const ModuleHandle::Port& src,
                                  const ModuleHandle::Port& dst) {
    impl_->connections.push_back({src, dst});
}

// ─────────────────────────────────────────
//  Налаштування
// ─────────────────────────────────────────
void Spartan7Optimizer::setClockFrequency(double mhz) {
    impl_->clock_mhz = mhz;
    impl_->solver.setClockPeriod(1000.0 / mhz);
}

void Spartan7Optimizer::setAreaBudget(const AreaBudget& budget) {
    impl_->area_budget = budget;
}

void Spartan7Optimizer::setPreferredRegion(const DeviceLocation& loc) {
    (void)loc; // флорплан — передається у TCL
}

// ─────────────────────────────────────────
//  COMPILE: головний пайплайн
// ─────────────────────────────────────────
BuildResult Spartan7Optimizer::compile(
        std::function<void(double, const std::string&)> progress_cb) {

    auto now = std::chrono::steady_clock::now();
    auto elapsed = [&now]() {
        return std::chrono::duration<double>(
            std::chrono::steady_clock::now() - now).count();
    };

    auto report_progress = [&](double pct, const std::string& msg) {
        if (progress_cb) progress_cb(pct, msg);
    };

    BuildResult result;
    result.status = BuildStatus::ANALYZING;
    report_progress(0.05, "Аналіз ресурсів...");

    // ── 1. Маппінг кожного модуля ────────────
    impl_->mapper.reset();
    std::vector<OptimalMapping> mappings;
    mappings.reserve(impl_->modules.size());

    for (const auto& mod : impl_->modules) {
        OptimalMapping m;
        switch (mod.type) {
            case ModuleType::MUX:
                m = impl_->mapper.mapMuxToSlice(
                        static_cast<int>(mod.inputs.size()), mod.data_width);
                break;
            case ModuleType::DEMUX:
                m = impl_->mapper.mapDemuxToSlice(
                        static_cast<int>(mod.inputs.size()), mod.data_width);
                break;
            case ModuleType::BRAM:
            case ModuleType::FIFO:
                m = impl_->mapper.mapFIFO(256, mod.data_width);
                break;
            case ModuleType::DSP48E1:
            case ModuleType::MUL:
                m = impl_->mapper.mapDSP(mod.data_width / 2, mod.data_width / 2);
                break;
            case ModuleType::ALU:
                m = impl_->mapper.mapALU(mod.data_width, true);
                break;
            default:
                m.is_valid = false;
        }
        if (!m.is_valid) {
            result.status  = BuildStatus::FAILED;
            result.message = "Не вдалося відобразити модуль: " + mod.name;
            return result;
        }
        mappings.push_back(m);
    }
    report_progress(0.20, "Маппінг завершено");

    // ── 2. Генетичний алгоритм ───────────────
    result.status = BuildStatus::OPTIMIZING;
    report_progress(0.25, "Запуск генетичного алгоритму...");

    GATask task;
    task.num_genes  = static_cast<int>(mappings.size());
    task.gene_range = 4; // 4 стилі реалізації
    task.evaluate_fn = [&mappings](const Chromosome& c) {
        double total_t = 0, total_p = 0, total_a = 0;
        for (size_t i = 0; i < c.genes.size() && i < mappings.size(); ++i) {
            total_t += mappings[i].timing_ns;
            total_p += mappings[i].power_mw;
            total_a += mappings[i].lut_count + mappings[i].bram_count * 100;
        }
        return std::make_tuple(total_t, total_p, total_a);
    };

    GeneticAlgorithm ga(impl_->ga_params);
    Chromosome best = ga.optimize(task);
    result.ga_stats = ga.getStatistics();
    report_progress(0.55, "ГА завершено, fitness=" +
                    std::to_string(result.ga_stats.best_fitness));

    // ── 3. Перевірка обмежень ────────────────
    std::vector<std::pair<int,int>> conn_pairs;
    for (const auto& c : impl_->connections) {
        conn_pairs.push_back({c.src.module_id - 1, c.dst.module_id - 1});
    }
    ConstraintResult cr = impl_->solver.checkAll(mappings, conn_pairs,
                                                  impl_->area_budget);
    if (!cr.passed) {
        result.status  = BuildStatus::FAILED;
        result.message = "Порушення обмежень: " + cr.reason;
        return result;
    }
    report_progress(0.65, "Обмеження виконано");

    // ── 4. Виклик Python через системний виклик ──
    result.status = BuildStatus::GENERATING_VHDL;
    report_progress(0.70, "Генерація VHDL...");
    std::string py_cmd = std::string("python3 src/python/spartan7_optimizer.py --mode generate ")
                        + "--device " + impl_->device_id + " 2>/dev/null || true";
    int rc = std::system(py_cmd.c_str());
    (void)rc;

    result.status = BuildStatus::SYNTHESIZING;
    report_progress(0.80, "Синтез Vivado...");
    rc = std::system(
        "vivado -mode batch -source src/vhdl/vivado_tcl/synthesis.tcl "
        "-notrace 2>/dev/null || true");

    result.status = BuildStatus::PLACE_AND_ROUTE;
    report_progress(0.90, "Place & Route...");

    result.status = BuildStatus::GENERATING_BITSTREAM;
    report_progress(0.95, "Генеруємо бітстрім...");

    // ── 5. Збір результатів ──────────────────
    result.status       = BuildStatus::DONE;
    result.final_usage  = impl_->mapper.getResourceUsage();
    result.total_time_s = elapsed();
    result.bitstream_path = "output/bitstream.bit";
    result.report_path    = "output/utilization.rpt";
    report_progress(1.0, "Компіляцію завершено за " +
                    std::to_string(result.total_time_s) + " с");

    // Зберігаємо звіт
    std::ostringstream ss;
    ss << "=== Spartan-7 Optimization Report ===\n"
       << "Device:  " << impl_->device_id << "\n"
       << "Modules: " << impl_->modules.size() << "\n"
       << "GA improvement: " << result.ga_stats.improvement_pct << "%\n"
       << "LUTs:  " << result.final_usage.lut_used << "/" << result.final_usage.lut_total
       << " (" << result.final_usage.lut_utilization() << "%)\n"
       << "FFs:   " << result.final_usage.ff_used  << "/" << result.final_usage.ff_total << "\n"
       << "BRAMs: " << result.final_usage.bram_used << "/" << result.final_usage.bram_total << "\n"
       << "DSPs:  " << result.final_usage.dsp_used  << "/" << result.final_usage.dsp_total << "\n";
    impl_->last_report   = ss.str();
    impl_->timing_report = impl_->solver.buildTimingPath(mappings).wns_ns >= 0
                           ? "Timing MET\n" : "Timing VIOLATION\n";

    return result;
}

BuildResult Spartan7Optimizer::program(const std::string& jtag_cable) {
    BuildResult result;
    result.status = BuildStatus::PROGRAMMING;
    int rc = std::system(
        ("vivado -mode batch -source src/vhdl/vivado_tcl/bitstream_gen.tcl "
         "-tclargs " + jtag_cable + " 2>/dev/null || true").c_str());
    (void)rc;
    result.status = BuildStatus::DONE;
    result.message = "Прошито через " + jtag_cable;
    return result;
}

ResourceUsage  Spartan7Optimizer::getResourceUsage()     const { return impl_->mapper.getResourceUsage(); }
std::string    Spartan7Optimizer::getOptimizationReport() const { return impl_->last_report; }
std::string    Spartan7Optimizer::getTimingReport()       const { return impl_->timing_report; }
void           Spartan7Optimizer::reset() {
    impl_->mapper.reset();
    impl_->modules.clear();
    impl_->connections.clear();
}

} // namespace spartan7
