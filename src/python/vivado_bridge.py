"""
vivado_bridge.py
Комунікація з Vivado: генерація TCL-скриптів, запуск процесу,
парсинг звітів про ресурси та timing.
"""

from __future__ import annotations

import json
import logging
import os
import re
import subprocess
import tempfile
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional

log = logging.getLogger(__name__)

# ──────────────────────────────────────────────
#  Структури результатів
# ──────────────────────────────────────────────
@dataclass
class ResourceUtilization:
    lut_used:  int = 0;  lut_total:  int = 0
    ff_used:   int = 0;  ff_total:   int = 0
    bram_used: int = 0;  bram_total: int = 0
    dsp_used:  int = 0;  dsp_total:  int = 0
    io_used:   int = 0;  io_total:   int = 0

    def lut_pct(self)  -> float: return 100 * self.lut_used  / max(self.lut_total,  1)
    def bram_pct(self) -> float: return 100 * self.bram_used / max(self.bram_total, 1)
    def dsp_pct(self)  -> float: return 100 * self.dsp_used  / max(self.dsp_total,  1)


@dataclass
class TimingReport:
    wns_ns:        float = 0.0   # Worst Negative Slack
    whs_ns:        float = 0.0   # Worst Hold Slack
    tns_ns:        float = 0.0   # Total Negative Slack
    timing_met:    bool  = True
    critical_path: str   = ""


# ──────────────────────────────────────────────
#  VivadoBridge
# ──────────────────────────────────────────────
class VivadoBridge:
    """
    Обгортка для Vivado ML Edition.
    Генерує TCL-скрипти, запускає vivado в batch-режимі,
    парсить звіти використання та timing.
    """

    def __init__(self, vivado_exe: str = "vivado", work_dir: str = "build"):
        self.vivado_exe = vivado_exe
        self.work_dir   = Path(work_dir)
        self.work_dir.mkdir(parents=True, exist_ok=True)
        self._vivado_available: Optional[bool] = None

    # ── Перевірка наявності Vivado ──────────
    def is_available(self) -> bool:
        if self._vivado_available is None:
            try:
                result = subprocess.run(
                    [self.vivado_exe, "-version"],
                    capture_output=True, text=True, timeout=10
                )
                self._vivado_available = result.returncode == 0
            except (FileNotFoundError, subprocess.TimeoutExpired):
                self._vivado_available = False
        return self._vivado_available

    # ── Аналіз пристрою ─────────────────────
    def run_device_analysis(self, device_id: str) -> Optional[ResourceUtilization]:
        """Запускає device_analyzer.tcl і повертає парсинг ресурсів."""
        tcl = self._create_device_analyzer_tcl(device_id)
        output = self._run_vivado_script(tcl)
        if output is None:
            return None
        return self._parse_utilization_report(output)

    def _create_device_analyzer_tcl(self, device_id: str) -> str:
        return f"""
# Auto-generated device analyzer
set device {device_id}
create_project -in_memory -part $device
open_hw_manager

# Звіт ресурсів пристрою
set device_info [get_parts $device]
puts "DEVICE_INFO:$device_info"

set lut_count  [get_property LUT_COUNT  [get_parts $device]]
set ff_count   [get_property FF_COUNT   [get_parts $device]]
set bram_count [get_property BRAM_COUNT [get_parts $device]]
set dsp_count  [get_property DSP_COUNT  [get_parts $device]]

puts "RESOURCE:LUT:$lut_count"
puts "RESOURCE:FF:$ff_count"
puts "RESOURCE:BRAM:$bram_count"
puts "RESOURCE:DSP:$dsp_count"
"""

    # ── Синтез ──────────────────────────────
    def run_synthesis(
        self,
        vhdl_files: list[str],
        device_id: str,
        clock_mhz: float = 100.0,
    ) -> bool:
        """Генерує TCL синтезу і запускає Vivado."""
        if not self.is_available():
            log.warning("Vivado не знайдено — синтез пропущено")
            return False

        tcl = self.create_synth_script(vhdl_files, device_id, clock_mhz)
        tcl_path = self.work_dir / "synthesis.tcl"
        tcl_path.write_text(tcl, encoding="utf-8")
        output = self._run_vivado_script(tcl_path)
        return output is not None and "ERROR" not in output.upper()

    def create_synth_script(
        self,
        vhdl_files: list[str],
        device_id: str,
        clock_mhz: float,
    ) -> str:
        period_ns = 1000.0 / clock_mhz
        files_tcl = "\n".join(
            f"  read_vhdl -vhdl2008 {{{f}}}" for f in vhdl_files
        )
        return f"""
# Auto-generated synthesis script
set device {device_id}
set period_ns {period_ns:.3f}
set output_dir {{{self.work_dir}/synth_output}}

# Зчитуємо джерела
{files_tcl}

# Синтез
synth_design -top top_wrapper -part $device \\
    -flatten_hierarchy rebuilt \\
    -directive AreaOptimized_high

# Timing constraint
create_clock -period $period_ns -name sys_clk [get_ports clk]

# Звіти
report_utilization   -file $output_dir/utilization_synth.rpt
report_timing_summary -file $output_dir/timing_synth.rpt

puts "SYNTHESIS_DONE"
"""

    # ── Place & Route ───────────────────────
    def create_pnr_script(self, device_id: str, clock_mhz: float) -> str:
        period_ns = 1000.0 / clock_mhz
        return f"""
# Auto-generated P&R script
open_checkpoint {{{self.work_dir}/synth_output/synth.dcp}}

opt_design
place_design -directive AltSpreadLogic_high
route_design -directive AggressiveExplore

# Звіти після P&R
report_utilization   -file {{{self.work_dir}/pnr_output/utilization_pnr.rpt}}
report_timing_summary -max_paths 10 \\
    -file {{{self.work_dir}/pnr_output/timing_pnr.rpt}}
report_congestion    -file {{{self.work_dir}/pnr_output/congestion.rpt}}

write_checkpoint -force {{{self.work_dir}/pnr_output/routed.dcp}}
puts "PNR_DONE"
"""

    # ── Бітстрім ────────────────────────────
    def generate_bitstream(self) -> Optional[str]:
        """Запускає bitstream_gen.tcl і повертає шлях до .bit файлу."""
        tcl_path = Path("src/vhdl/vivado_tcl/bitstream_gen.tcl")
        output = self._run_vivado_script(tcl_path)
        if output and "BITSTREAM_DONE" in output:
            bit_path = str(self.work_dir / "output" / "design.bit")
            return bit_path
        return None

    # ── Парсинг звітів ──────────────────────
    def parse_device_report(self, raw: str) -> ResourceUtilization:
        return self._parse_utilization_report(raw)

    def extract_resource_utilization(self, report_path: str) -> ResourceUtilization:
        try:
            text = Path(report_path).read_text(encoding="utf-8")
            return self._parse_utilization_report(text)
        except OSError as e:
            log.error("Не вдалося прочитати звіт: %s", e)
            return ResourceUtilization()

    def _parse_utilization_report(self, text: str) -> ResourceUtilization:
        util = ResourceUtilization()
        # Парсинг Vivado utilization report таблиць (Slice LUTs, Slice Registers тощо)
        patterns = {
            "lut_used":   r"Slice LUTs\s*\|\s*(\d+)",
            "lut_total":  r"Slice LUTs\s*\|\s*\d+\s*\|\s*\d+\s*\|\s*(\d+)",
            "ff_used":    r"Slice Registers\s*\|\s*(\d+)",
            "bram_used":  r"Block RAM Tile\s*\|\s*(\d+)",
            "bram_total": r"Block RAM Tile\s*\|\s*\d+\s*\|\s*\d+\s*\|\s*(\d+)",
            "dsp_used":   r"DSPs\s*\|\s*(\d+)",
        }
        # Також парсимо custom RESOURCE: рядки від нашого TCL
        for line in text.splitlines():
            m = re.match(r"RESOURCE:LUT:(\d+)", line)
            if m:
                util.lut_total = int(m.group(1))
            m = re.match(r"RESOURCE:FF:(\d+)", line)
            if m:
                util.ff_total = int(m.group(1))
            m = re.match(r"RESOURCE:BRAM:(\d+)", line)
            if m:
                util.bram_total = int(m.group(1))
            m = re.match(r"RESOURCE:DSP:(\d+)", line)
            if m:
                util.dsp_total = int(m.group(1))

        for field_name, pattern in patterns.items():
            m = re.search(pattern, text, re.IGNORECASE)
            if m:
                setattr(util, field_name, int(m.group(1)))
        return util

    def parse_timing_report(self, text: str) -> TimingReport:
        tr = TimingReport()
        m = re.search(r"WNS\(ns\)\s*[\|:]\s*(-?[\d.]+)", text)
        if m:
            tr.wns_ns = float(m.group(1))
        m = re.search(r"WHS\(ns\)\s*[\|:]\s*(-?[\d.]+)", text)
        if m:
            tr.whs_ns = float(m.group(1))
        m = re.search(r"TNS\(ns\)\s*[\|:]\s*(-?[\d.]+)", text)
        if m:
            tr.tns_ns = float(m.group(1))
        tr.timing_met = (tr.wns_ns >= 0.0 and tr.whs_ns >= 0.0)
        return tr

    # ── Запуск Vivado ────────────────────────
    def _run_vivado_script(self, tcl_source) -> Optional[str]:
        """
        tcl_source: Path до .tcl файлу або рядок TCL-коду.
        Повертає stdout або None при помилці.
        """
        if not self.is_available():
            return None

        if isinstance(tcl_source, str) and not Path(tcl_source).exists():
            # Записуємо рядок у тимчасовий файл
            tmp = tempfile.NamedTemporaryFile(
                mode="w", suffix=".tcl", delete=False, encoding="utf-8"
            )
            tmp.write(tcl_source)
            tmp.close()
            tcl_path = tmp.name
        else:
            tcl_path = str(tcl_source)

        cmd = [self.vivado_exe, "-mode", "batch", "-source", tcl_path, "-notrace"]
        try:
            result = subprocess.run(
                cmd,
                capture_output=True,
                text=True,
                timeout=300,         # 5 хвилин максимум
                cwd=str(self.work_dir),
            )
            if result.returncode != 0:
                log.error("Vivado завершився з кодом %d:\n%s",
                          result.returncode, result.stderr[-2000:])
                return None
            return result.stdout
        except subprocess.TimeoutExpired:
            log.error("Vivado timeout — перевищено 300с")
            return None
        except Exception as exc:  # noqa: BLE001
            log.error("Vivado run failed: %s", exc)
            return None
        finally:
            if isinstance(tcl_source, str) and not Path(tcl_source).exists():
                try:
                    os.unlink(tcl_path)
                except OSError:
                    pass
