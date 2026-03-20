## =============================================================================
## synthesis.tcl
## Синтез, Place & Route проєкту Spartan-7 з оптимальними параметрами
## Запуск: vivado -mode batch -source synthesis.tcl -notrace
##         -tclargs <device_id> <top_entity> <clock_mhz> <work_dir>
## =============================================================================

## ── Парсинг аргументів ────────────────────────────────────────────────────────
if {[llength $argv] >= 1} { set device_id   [lindex $argv 0] } else { set device_id   "xc7s50-1fgg484" }
if {[llength $argv] >= 2} { set top_entity  [lindex $argv 1] } else { set top_entity  "top_wrapper"     }
if {[llength $argv] >= 3} { set clock_mhz   [lindex $argv 2] } else { set clock_mhz   100.0             }
if {[llength $argv] >= 4} { set work_dir    [lindex $argv 3] } else { set work_dir    "build"           }

set period_ns   [expr {1000.0 / $clock_mhz}]
set synth_dir   "$work_dir/synth_output"
set pnr_dir     "$work_dir/pnr_output"

file mkdir $synth_dir
file mkdir $pnr_dir

puts "=== Spartan-7 Synthesis & P&R ==="
puts "Device   : $device_id"
puts "Top      : $top_entity"
puts "Clock    : $clock_mhz MHz ($period_ns ns)"
puts "Work dir : $work_dir"

## ── Допоміжні процедури ──────────────────────────────────────────────────────

proc read_vhdl_sources {work_dir} {
    # Зчитуємо всі VHDL файли зі стандартних директорій
    set vhdl_dirs [list \
        "$work_dir/generated_vhdl" \
        "src/vhdl" \
    ]

    set read_count 0
    foreach dir $vhdl_dirs {
        if {[file exists $dir]} {
            foreach f [glob -nocomplain "$dir/*.vhd" "$dir/*.vhdl"] {
                puts "INFO: Читаємо $f"
                read_vhdl -vhdl2008 $f
                incr read_count
            }
        }
    }
    puts "INFO: Зчитано $read_count VHDL файлів"
    return $read_count
}

proc apply_synthesis_strategies {directive} {
    # Вибір стратегії синтезу залежно від мети оптимізації
    # "AreaOptimized_high"  — мінімум LUT
    # "PerformanceOptimized" — мінімум затримки
    # "RuntimeOptimized"    — швидкий синтез для тестування
    set valid [list AreaOptimized_high AreaOptimized_medium \
                    PerformanceOptimized AlternateRoutability \
                    RuntimeOptimized Default]
    if {$directive ni $valid} {
        puts "WARNING: Невідома стратегія $directive, використовуємо Default"
        return "Default"
    }
    return $directive
}

## ── Зчитування джерел ────────────────────────────────────────────────────────
set n_sources [read_vhdl_sources $work_dir]
if {$n_sources == 0} {
    puts "ERROR: VHDL джерела не знайдено. Перевірте $work_dir/generated_vhdl"
    exit 1
}

## ── Синтез ───────────────────────────────────────────────────────────────────
set synth_directive [apply_synthesis_strategies "AreaOptimized_high"]

puts "\nINFO: Запуск синтезу (directive: $synth_directive)..."

synth_design \
    -top     $top_entity \
    -part    $device_id \
    -directive $synth_directive \
    -flatten_hierarchy rebuilt \
    -keep_equivalent_registers \
    -no_lc

## ── Timing constraint ────────────────────────────────────────────────────────
create_clock \
    -period $period_ns \
    -name   sys_clk \
    [get_ports -quiet clk]

# I/O timing constraints (по 20% від clock period)
set io_margin [expr {$period_ns * 0.2}]
set_input_delay  -clock sys_clk $io_margin [all_inputs]
set_output_delay -clock sys_clk $io_margin [all_outputs]

## ── Звіти після синтезу ──────────────────────────────────────────────────────
report_timing_summary \
    -max_paths 10 \
    -report_unconstrained \
    -file "$synth_dir/timing_summary_synth.rpt"

report_utilization \
    -file "$synth_dir/utilization_synth.rpt"

## ── Checkpoint ───────────────────────────────────────────────────────────────
write_checkpoint -force "$synth_dir/synth.dcp"
puts "INFO: Синтез завершено. Checkpoint: $synth_dir/synth.dcp"

## ── Оптимізація ──────────────────────────────────────────────────────────────
puts "\nINFO: opt_design..."
opt_design

## ── Place ────────────────────────────────────────────────────────────────────
puts "\nINFO: place_design (AltSpreadLogic_high)..."
place_design -directive AltSpreadLogic_high

phys_opt_design

## ── Route ────────────────────────────────────────────────────────────────────
puts "\nINFO: route_design (AggressiveExplore)..."
route_design -directive AggressiveExplore

## ── Перевірка design після routing ──────────────────────────────────────────
set timing_slack [get_property SLACK [get_timing_paths -max_paths 1 -nworst 1 -quiet]]
if {$timing_slack ne "" && [expr {$timing_slack < 0}]} {
    puts "WARNING: Timing violation! WNS = $timing_slack ns"
    puts "WARNING: Спробуйте зменшити clock_mhz або переглянути floorplan"
} else {
    puts "INFO: Timing met. WNS >= 0 ns"
}

## ── Звіти після P&R ──────────────────────────────────────────────────────────
report_timing_summary \
    -max_paths 10 \
    -report_unconstrained \
    -file "$pnr_dir/timing_summary_pnr.rpt"

report_utilization \
    -hierarchical \
    -file "$pnr_dir/utilization_pnr.rpt"

report_congestion \
    -file "$pnr_dir/congestion.rpt"

report_route_status \
    -file "$pnr_dir/route_status.rpt"

report_power \
    -file "$pnr_dir/power.rpt"

## ── Checkpoint після P&R ─────────────────────────────────────────────────────
write_checkpoint -force "$pnr_dir/routed.dcp"
puts "INFO: P&R завершено. Checkpoint: $pnr_dir/routed.dcp"

puts "\nSYNTHESIS_DONE"
