#pragma once

#include "hardware_mapper.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <optional>

namespace spartan7 {

// ─────────────────────────────────────────────
//  Тип модуля
// ─────────────────────────────────────────────
enum class ModuleType {
    MUX, DEMUX,
    BRAM, FIFO,
    DSP48E1,
    ALU, MUL,
    SHIFTER,
    COMPARATOR,
    ENCODER, DECODER,
    COUNTER,
    CUSTOM
};

// ─────────────────────────────────────────────
//  Мінімальні вимоги до ресурсів модуля
// ─────────────────────────────────────────────
struct ResourceRequirement {
    int min_lut   = 0;
    int min_ff    = 0;
    int min_bram  = 0;
    int min_dsp   = 0;
    double min_timing_budget_ns = 1.0; // мінімально необхідний slack
};

// ─────────────────────────────────────────────
//  Запис у реєстрі модулів
// ─────────────────────────────────────────────
struct ModuleInfo {
    std::string          name;
    ModuleType           type   = ModuleType::CUSTOM;
    std::string          description;
    std::string          vhdl_template;     // ім'я VHDL-шаблону
    ResourceRequirement  requirements;
    std::vector<std::string> alternatives;  // імена альтернативних реалізацій

    // Параметри інстанціювання (ключ → значення)
    std::unordered_map<std::string, std::string> default_generics;
};

// ─────────────────────────────────────────────
//  Запит до реєстру
// ─────────────────────────────────────────────
struct ModuleQuery {
    ModuleType              type;
    int                     data_width  = 8;
    int                     depth       = 0;   // для FIFO/BRAM
    int                     inputs      = 2;   // для MUX/DEMUX
    ImplementationStyle     preferred   = ImplementationStyle::AUTO;
};

// ─────────────────────────────────────────────
//  Реєстр модулів (Singleton)
// ─────────────────────────────────────────────
class ModuleRegistry {
public:
    static ModuleRegistry& instance();

    // Ручна реєстрація користувацького модуля
    void registerModule(const ModuleInfo& info);

    // Пошук за іменем
    std::optional<ModuleInfo> queryByName(const std::string& name) const;

    // Пошук за типом і параметрами
    std::optional<ModuleInfo> queryModule(const ModuleQuery& query) const;

    // Список альтернативних реалізацій
    std::vector<ModuleInfo> getAlternatives(const std::string& name) const;

    // Всі зареєстровані модулі певного типу
    std::vector<ModuleInfo> getByType(ModuleType type) const;

    // Підрахунок вимог до ресурсів для списку модулів
    ResourceRequirement aggregateRequirements(
        const std::vector<std::string>& module_names) const;

    // Статистика реєстру
    int totalCount() const { return static_cast<int>(registry_.size()); }

private:
    ModuleRegistry();   // приватний конструктор — Singleton
    ModuleRegistry(const ModuleRegistry&) = delete;
    ModuleRegistry& operator=(const ModuleRegistry&) = delete;

    void populateBuiltins();  // реєстрація вбудованих Spartan-7 модулів

    std::unordered_map<std::string, ModuleInfo> registry_;
};

} // namespace spartan7
