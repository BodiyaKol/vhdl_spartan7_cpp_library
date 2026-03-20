#include "module_registry.hpp"
#include <stdexcept>
#include <algorithm>

namespace spartan7 {

// ─────────────────────────────────────────
//  Singleton
// ─────────────────────────────────────────
ModuleRegistry& ModuleRegistry::instance() {
    static ModuleRegistry inst;
    return inst;
}

ModuleRegistry::ModuleRegistry() {
    populateBuiltins();
}

// ─────────────────────────────────────────
//  Вбудовані модулі Spartan-7
// ─────────────────────────────────────────
void ModuleRegistry::populateBuiltins() {
    // ── MUX ─────────────────────────────────
    ModuleInfo mux;
    mux.name          = "MUX_GENERIC";
    mux.type          = ModuleType::MUX;
    mux.description   = "Параметризований мультиплексор (2..256 входів, 1..64 біт)";
    mux.vhdl_template = "mux_generic.vhdl";
    mux.requirements  = {.min_lut = 1, .min_ff = 0, .min_timing_budget_ns = 0.5};
    mux.default_generics = {{"WIDTH","8"},{"INPUTS","2"},{"IMPLEMENTATION","LUT"}};
    mux.alternatives  = {"SRL_MUX","BRAM_MUX"};
    registry_["MUX_GENERIC"] = mux;

    ModuleInfo srl_mux = mux;
    srl_mux.name = "SRL_MUX";
    srl_mux.description = "MUX на SRL16/SRL32 — компактніший для 16/32 входів";
    srl_mux.default_generics["IMPLEMENTATION"] = "SRL";
    registry_["SRL_MUX"] = srl_mux;

    // ── DEMUX ────────────────────────────────
    ModuleInfo demux;
    demux.name          = "DEMUX_GENERIC";
    demux.type          = ModuleType::DEMUX;
    demux.description   = "Параметризований демультиплексор";
    demux.vhdl_template = "demux_generic.vhdl";
    demux.requirements  = {.min_lut = 1, .min_ff = 0, .min_timing_budget_ns = 0.5};
    demux.default_generics = {{"WIDTH","8"},{"OUTPUTS","2"}};
    demux.alternatives  = {"MUX_GENERIC"}; // MUX з інвертованою логікою вибору
    registry_["DEMUX_GENERIC"] = demux;

    // ── BRAM 36Kb ────────────────────────────
    ModuleInfo bram;
    bram.name          = "BRAM36K";
    bram.type          = ModuleType::BRAM;
    bram.description   = "Блочна RAM 36Kb з двопортовим доступом";
    bram.vhdl_template = "memory_controller.vhdl";
    bram.requirements  = {.min_lut = 4, .min_ff = 8, .min_bram = 1, .min_timing_budget_ns = 2.0};
    bram.default_generics = {{"DEPTH","1024"},{"WIDTH","36"},{"LATENCY","1"}};
    bram.alternatives  = {"DIST_RAM"};
    registry_["BRAM36K"] = bram;

    ModuleInfo dist_ram = bram;
    dist_ram.name = "DIST_RAM";
    dist_ram.type = ModuleType::BRAM;
    dist_ram.description = "Розподілена RAM на LUT-ах (до 64 слів)";
    dist_ram.requirements = {.min_lut = 8, .min_ff = 0, .min_timing_budget_ns = 1.5};
    dist_ram.default_generics = {{"DEPTH","64"},{"WIDTH","8"}};
    registry_["DIST_RAM"] = dist_ram;

    // ── FIFO ─────────────────────────────────
    ModuleInfo fifo;
    fifo.name          = "FIFO_GENERIC";
    fifo.type          = ModuleType::FIFO;
    fifo.description   = "Synchronous FIFO з сигналами full/empty/count";
    fifo.vhdl_template = "memory_controller.vhdl";
    fifo.requirements  = {.min_lut = 12, .min_ff = 16, .min_bram = 1, .min_timing_budget_ns = 2.5};
    fifo.default_generics = {{"DEPTH","256"},{"WIDTH","8"}};
    fifo.alternatives  = {"BRAM36K"};
    registry_["FIFO_GENERIC"] = fifo;

    // ── DSP48E1 ──────────────────────────────
    ModuleInfo dsp;
    dsp.name          = "DSP48E1";
    dsp.type          = ModuleType::DSP48E1;
    dsp.description   = "DSP48E1 slice: 25x18 bit multiply-accumulate";
    dsp.vhdl_template = "wrapper_template.vhdl";
    dsp.requirements  = {.min_lut = 2, .min_ff = 4, .min_dsp = 1, .min_timing_budget_ns = 3.8};
    dsp.default_generics = {{"A_WIDTH","25"},{"B_WIDTH","18"},{"USE_ACCU","TRUE"}};
    registry_["DSP48E1"] = dsp;

    // ── ALU ──────────────────────────────────
    ModuleInfo alu;
    alu.name          = "ALU_GENERIC";
    alu.type          = ModuleType::ALU;
    alu.description   = "Арифметично-логічний пристрій на CARRY4 ланцюжках";
    alu.vhdl_template = "wrapper_template.vhdl";
    alu.requirements  = {.min_lut = 8, .min_ff = 8, .min_timing_budget_ns = 1.0};
    alu.default_generics = {{"WIDTH","8"},{"OPS","ADD,SUB,AND,OR,XOR,NOT"}};
    alu.alternatives  = {"DSP48E1"};
    registry_["ALU_GENERIC"] = alu;

    // ── MUL ──────────────────────────────────
    ModuleInfo mul;
    mul.name          = "MUL_GENERIC";
    mul.type          = ModuleType::MUL;
    mul.description   = "Множник; автоматичне відображення на DSP або LUT";
    mul.vhdl_template = "wrapper_template.vhdl";
    mul.requirements  = {.min_dsp = 1, .min_timing_budget_ns = 4.0};
    mul.default_generics = {{"A_WIDTH","8"},{"B_WIDTH","8"},{"PIPELINE","1"}};
    mul.alternatives  = {"DSP48E1","ALU_GENERIC"};
    registry_["MUL_GENERIC"] = mul;
}

// ─────────────────────────────────────────
//  API
// ─────────────────────────────────────────
void ModuleRegistry::registerModule(const ModuleInfo& info) {
    registry_[info.name] = info;
}

std::optional<ModuleInfo> ModuleRegistry::queryByName(const std::string& name) const {
    auto it = registry_.find(name);
    if (it == registry_.end()) return std::nullopt;
    return it->second;
}

std::optional<ModuleInfo> ModuleRegistry::queryModule(const ModuleQuery& q) const {
    for (const auto& [key, info] : registry_) {
        if (info.type != q.type) continue;
        // Перевірка preferred style через тег вhдl_template або alternatives
        if (q.preferred != ImplementationStyle::AUTO) {
            if (q.preferred == ImplementationStyle::SRL_BASED
                && key.find("SRL") == std::string::npos) continue;
            if (q.preferred == ImplementationStyle::BRAM_BASED
                && key.find("BRAM") == std::string::npos
                && key.find("FIFO") == std::string::npos) continue;
        }
        return info;
    }
    return std::nullopt;
}

std::vector<ModuleInfo> ModuleRegistry::getAlternatives(const std::string& name) const {
    auto base = queryByName(name);
    if (!base) return {};
    std::vector<ModuleInfo> result;
    for (const auto& alt_name : base->alternatives) {
        auto alt = queryByName(alt_name);
        if (alt) result.push_back(*alt);
    }
    return result;
}

std::vector<ModuleInfo> ModuleRegistry::getByType(ModuleType type) const {
    std::vector<ModuleInfo> result;
    for (const auto& [key, info] : registry_) {
        if (info.type == type) result.push_back(info);
    }
    return result;
}

ResourceRequirement ModuleRegistry::aggregateRequirements(
        const std::vector<std::string>& names) const {
    ResourceRequirement total{};
    for (const auto& n : names) {
        auto info = queryByName(n);
        if (!info) continue;
        total.min_lut  += info->requirements.min_lut;
        total.min_ff   += info->requirements.min_ff;
        total.min_bram += info->requirements.min_bram;
        total.min_dsp  += info->requirements.min_dsp;
        total.min_timing_budget_ns = std::max(total.min_timing_budget_ns,
                                               info->requirements.min_timing_budget_ns);
    }
    return total;
}

} // namespace spartan7
