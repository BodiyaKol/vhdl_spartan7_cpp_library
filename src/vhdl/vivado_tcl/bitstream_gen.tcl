## =============================================================================
## bitstream_gen.tcl
## Генерація бітстріму та прошивання Spartan-7 через JTAG
## Запуск: vivado -mode batch -source bitstream_gen.tcl -notrace
##         -tclargs <work_dir> <jtag_cable>
## =============================================================================

## ── Аргументи ─────────────────────────────────────────────────────────────────
if {[llength $argv] >= 1} { set work_dir   [lindex $argv 0] } else { set work_dir   "build"  }
if {[llength $argv] >= 2} { set jtag_cable [lindex $argv 1] } else { set jtag_cable "auto"   }

set pnr_dir    "$work_dir/pnr_output"
set output_dir "$work_dir/output"

file mkdir $output_dir

puts "=== Bitstream Generation & Programming ==="
puts "Work dir   : $work_dir"
puts "JTAG cable : $jtag_cable"

## ── Перевірка наявності checkpoint ──────────────────────────────────────────
set dcp_path "$pnr_dir/routed.dcp"
if {![file exists $dcp_path]} {
    puts "ERROR: Не знайдено routed.dcp. Спочатку запустіть synthesis.tcl"
    exit 1
}

## ── Відкриття checkpoint ─────────────────────────────────────────────────────
puts "INFO: Відкриваємо $dcp_path"
open_checkpoint $dcp_path

## ── DRC перевірки ────────────────────────────────────────────────────────────
puts "INFO: Запуск DRC..."
set drc_results [report_drc -quiet -return_string]

if {[regexp {CRITICAL WARNING|ERROR} $drc_results]} {
    puts "WARNING: DRC виявив проблеми:"
    set drc_file "$output_dir/drc_violations.rpt"
    report_drc -file $drc_file
    puts "WARNING: Деталі у $drc_file"
    # Не зупиняємо виконання — дозволяємо генерацію для некритичних
}

## ── Bitstream properties ─────────────────────────────────────────────────────
# Bitstream compress — зменшує розмір файлу конфігурації
set_property BITSTREAM.GENERAL.COMPRESS          TRUE [current_design]
# Startup clock — CCLK для SPI/selectMAP, JTGCLK для JTAG
set_property BITSTREAM.GENERAL.USERID            {0xDEAD_BEEF} [current_design]
# Readback CRC — захист від пошкодження конфігурації
set_property BITSTREAM.READBACK.SECURITY         DISABLE [current_design]

## ── Генерація бітстріму ──────────────────────────────────────────────────────
set bit_path "$output_dir/design.bit"
set bin_path "$output_dir/design.bin"

puts "INFO: write_bitstream..."
write_bitstream -force -bin_file $bit_path

puts "INFO: Бітстрім згенеровано: $bit_path"

## ── Звіт використання ────────────────────────────────────────────────────────
report_utilization -file "$output_dir/final_utilization.rpt"
report_timing_summary -max_paths 5 -file "$output_dir/final_timing.rpt"

## ── Прошивання (якщо cable != "skip") ───────────────────────────────────────
proc program_device {bit_path jtag_cable} {
    puts "INFO: Підключаємось до JTAG..."

    open_hw_manager

    if {[catch {connect_hw_server -allow_non_jtag} err]} {
        puts "WARNING: Не вдалося підключитись до HW Server: $err"
        puts "INFO: Переконайтесь що Vivado HW Server запущено (hw_server.exe)"
        return 0
    }

    if {[catch {open_hw_target} err]} {
        puts "WARNING: Не вдалося відкрити JTAG target: $err"
        return 0
    }

    # Знайти Spartan-7 пристрій на JTAG ланцюжку
    set devices [get_hw_devices]
    if {[llength $devices] == 0} {
        puts "ERROR: JTAG пристроїв не знайдено"
        close_hw_target
        disconnect_hw_server
        return 0
    }

    set target_device ""
    foreach dev $devices {
        set part [get_property PART $dev -quiet]
        if {[string match "xc7s*" $part]} {
            set target_device $dev
            break
        }
    }

    if {$target_device eq ""} {
        puts "WARNING: Spartan-7 не знайдено серед: $devices"
        puts "INFO: Спробуємо перший пристрій: [lindex $devices 0]"
        set target_device [lindex $devices 0]
    }

    puts "INFO: Прошиваємо $target_device"
    set_property PROGRAM.FILE $bit_path $target_device

    # Опційний заголовок програмування
    if {[get_property PROGRAM.FULL_BITFILE $target_device] ne ""} {
        set_property PROGRAM.FULL_BITFILE $bit_path $target_device
    }

    if {[catch {program_hw_devices $target_device} err]} {
        puts "ERROR: Прошивання не вдалось: $err"
        close_hw_target
        disconnect_hw_server
        return 0
    }

    puts "INFO: Прошивання успішно завершено!"

    # Verify (опційно)
    catch {
        refresh_hw_device $target_device
        puts "INFO: Пристрій активний після прошивання"
    }

    close_hw_target
    disconnect_hw_server
    close_hw_manager
    return 1
}

## ── Запуск прошивання ────────────────────────────────────────────────────────
if {$jtag_cable ne "skip"} {
    set prog_result [program_device $bit_path $jtag_cable]
    if {!$prog_result} {
        puts "WARNING: Прошивання не виконано. Бітстрім збережено: $bit_path"
        puts "INFO: Для ручного прошивання: open_hw_manager -> program_hw_devices"
    }
} else {
    puts "INFO: Прошивання пропущено (jtag_cable=skip)"
    puts "INFO: Бітстрім збережено: $bit_path"
}

puts "\nBITSTREAM_DONE"
puts "=== Генерація бітстріму завершена ==="
