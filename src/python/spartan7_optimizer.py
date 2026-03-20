"""
spartan7_optimizer.py
Головний оркестратор: приймає опис схеми від C++ або напряму з Python,
запускає аналіз ресурсів, генетичний алгоритм і генерацію VHDL.
"""

from __future__ import annotations

import argparse
import json
import logging
import os
import subprocess
from dataclasses import dataclass, field
from enum import Enum, auto
from pathlib import Path
from typing import Callable, Optional

from vivado_bridge import VivadoBridge
from hardware_analyzer import HardwareAnalyzer, DeviceProfile
from code_generator import CodeGenerator

log = logging.getLogger(__name__)


# ──────────────────────────────────────────────
#  Типи підтримуваних модулів
# ──────────────────────────────────────────────
class ModuleKind(str, Enum):
    MUX      = "MUX"
    DEMUX    = "DEMUX"
    RAM      = "RAM"
    FIFO     = "FIFO"
    ALU      = "ALU"
    MUL      = "MUL"
    DSP48E1  = "DSP48E1"


@dataclass
class ModuleSpec:
    """Опис одного логічного модуля."""
    kind:       ModuleKind
    name:       str
    data_width: int = 8
    inputs:     int = 2       # для MUX/DEMUX
    depth:      int = 256     # для RAM/FIFO
    a_width:    int = 8       # для DSP/MUL
    b_width:    int = 8


@dataclass
class Connection:
    src_name:  str
    src_port:  str
    dst_name:  str
    dst_port:  str


@dataclass
class OptimizationResult:
    success:       bool
    device_profile: Optional[DeviceProfile] = None
    vhdl_files:    list[str]     = field(default_factory=list)
    bitstream_path: Optional[str] = None
    report:        str = ""
    ga_improvement: float = 0.0   # відсоток покращення fitness


# ──────────────────────────────────────────────
#  Головний клас
# ──────────────────────────────────────────────
class Spartan7Optimizer:
    """
    Головний Python API для бібліотеки.

    Приклад:
        opt = Spartan7Optimizer("xc7s50-1fgg484")
        m1 = opt.add_mux(8, 2, name="sel_mux")
        opt.compile()
    """

    SUPPORTED_DEVICES = [
        "xc7s6",  "xc7s15", "xc7s25",
        "xc7s50", "xc7s75", "xc7s100",
    ]

    def __init__(
        self,
        device_id: str,
        clock_mhz: float = 100.0,
        work_dir: str = "build",
        vivado_path: str = "vivado",
    ):
        self._validate_device(device_id)
        self.device_id   = device_id
        self.clock_mhz   = clock_mhz
        self.work_dir    = Path(work_dir)
        self.work_dir.mkdir(parents=True, exist_ok=True)

        self._modules:     list[ModuleSpec] = []
        self._connections: list[Connection] = []

        self._bridge   = VivadoBridge(vivado_path, str(self.work_dir))
        self._analyzer = HardwareAnalyzer(self._bridge)
        self._codegen  = CodeGenerator(str(self.work_dir))

        logging.basicConfig(level=logging.INFO,
                            format="[%(levelname)s] %(message)s")

    # ── Додавання модулів ──────────────────
    def add_mux(self, inputs: int, data_width: int = 1,
                name: str = "") -> ModuleSpec:
        return self._add(ModuleSpec(
            kind=ModuleKind.MUX, name=name or f"mux_{len(self._modules)}",
            inputs=inputs, data_width=data_width))

    def add_demux(self, outputs: int, data_width: int = 1,
                  name: str = "") -> ModuleSpec:
        return self._add(ModuleSpec(
            kind=ModuleKind.DEMUX, name=name or f"demux_{len(self._modules)}",
            inputs=outputs, data_width=data_width))

    def add_ram(self, depth: int, data_width: int = 8,
                name: str = "") -> ModuleSpec:
        return self._add(ModuleSpec(
            kind=ModuleKind.RAM, name=name or f"ram_{len(self._modules)}",
            depth=depth, data_width=data_width))

    def add_fifo(self, depth: int, data_width: int = 8,
                 name: str = "") -> ModuleSpec:
        return self._add(ModuleSpec(
            kind=ModuleKind.FIFO, name=name or f"fifo_{len(self._modules)}",
            depth=depth, data_width=data_width))

    def add_dsp(self, a_width: int = 18, b_width: int = 18,
                name: str = "") -> ModuleSpec:
        return self._add(ModuleSpec(
            kind=ModuleKind.DSP48E1, name=name or f"dsp_{len(self._modules)}",
            a_width=a_width, b_width=b_width))

    def add_alu(self, data_width: int = 8, name: str = "") -> ModuleSpec:
        return self._add(ModuleSpec(
            kind=ModuleKind.ALU, name=name or f"alu_{len(self._modules)}",
            data_width=data_width))

    def connect(self, src_name: str, src_port: str,
                dst_name: str, dst_port: str) -> None:
        self._connections.append(Connection(src_name, src_port, dst_name, dst_port))

    # ── Головний пайплайн ─────────────────
    def compile(
        self,
        progress: Callable[[float, str], None] | None = None,
    ) -> OptimizationResult:
        """
        Повний цикл: аналіз → ГА → генерація VHDL → синтез → P&R.
        """
        def _p(pct: float, msg: str) -> None:
            log.info("[%.0f%%] %s", pct * 100, msg)
            if progress:
                progress(pct, msg)

        _p(0.00, "Старт компіляції")

        # 1. Аналіз пристрою
        _p(0.05, "Аналіз ресурсів Spartan-7")
        try:
            profile = self._analyzer.scan(self.device_id)
        except Exception as exc:
            log.warning("Vivado недоступний, використовую статичні дані: %s", exc)
            profile = self._analyzer.get_static_profile(self.device_id)

        # 2. Оцінка початкового маппінгу
        _p(0.15, "Оцінка вимог модулів")
        requirements = self._estimate_requirements()

        # 3. Генетичний алгоритм
        _p(0.25, "Генетичний алгоритм — пошук оптимуму")
        best_config, improvement = self._run_ga(requirements, profile)

        # 4. Генерація VHDL
        _p(0.60, "Генерація VHDL шаблонів")
        vhdl_files = self._codegen.generate_vhdl(
            self._modules, self._connections, best_config, self.device_id
        )

        # 5. Синтез (якщо Vivado доступний)
        _p(0.75, "Виклик Vivado синтезу")
        synth_ok = self._bridge.run_synthesis(
            vhdl_files, self.device_id, self.clock_mhz
        )

        # 6. Звіт
        _p(1.00, "Компіляцію завершено")
        report = self._build_report(profile, best_config, improvement)

        return OptimizationResult(
            success=synth_ok,
            device_profile=profile,
            vhdl_files=vhdl_files,
            report=report,
            ga_improvement=improvement,
        )

    def generate_bitstream(self) -> OptimizationResult:
        """Генерує та завантажує бітстрім на FPGA."""
        bit_path = self._bridge.generate_bitstream()
        return OptimizationResult(
            success=bit_path is not None,
            bitstream_path=bit_path,
            report="Bitstream: " + (bit_path or "FAILED"),
        )

    # ── Внутрішні методи ──────────────────
    def _add(self, spec: ModuleSpec) -> ModuleSpec:
        self._modules.append(spec)
        return spec

    def _validate_device(self, dev: str) -> None:
        if not any(dev.startswith(d) for d in self.SUPPORTED_DEVICES):
            raise ValueError(
                f"Непідтримуваний пристрій: {dev!r}. "
                f"Підтримуються: {self.SUPPORTED_DEVICES}"
            )

    def _estimate_requirements(self) -> dict:
        """Повертає сумарні вимоги до ресурсів."""
        lut = ff = bram = dsp = 0
        for m in self._modules:
            if m.kind == ModuleKind.MUX:
                lut += max(1, m.inputs) * m.data_width
            elif m.kind == ModuleKind.DEMUX:
                lut += max(1, m.inputs) * m.data_width
            elif m.kind in (ModuleKind.RAM, ModuleKind.FIFO):
                total_bits = m.depth * m.data_width
                bram += max(1, total_bits // (36 * 1024))
                lut += 12
                ff  += 16
            elif m.kind in (ModuleKind.DSP48E1, ModuleKind.MUL):
                dsp += 1
            elif m.kind == ModuleKind.ALU:
                lut += m.data_width
                ff  += m.data_width
        return {"lut": lut, "ff": ff, "bram": bram, "dsp": dsp}

    def _run_ga(
        self, requirements: dict, profile: DeviceProfile
    ) -> tuple[dict, float]:
        """
        Спрощений GA на Python для вибору стилю реалізації кожного модуля.
        Повноцінний ГА реалізовано в C++ (genetic_algorithm.cpp).
        """
        import random, copy

        styles = ["LUT", "SRL", "BRAM", "DSP"]
        pop_size, generations = 50, 30
        mut_rate = 0.15

        def rand_chromosome():
            return [random.choice(styles) for _ in self._modules]

        def fitness(chrom: list[str]) -> float:
            speed_score = area_score = power_score = 0.0
            for i, m in enumerate(self._modules):
                style = chrom[i]
                if style == "LUT":
                    speed_score  += 0.9
                    area_score   += 0.6
                    power_score  += 0.8
                elif style == "SRL":
                    speed_score  += 0.7
                    area_score   += 0.9
                    power_score  += 0.85
                elif style == "BRAM":
                    speed_score  += 0.6
                    area_score   += 1.0 if m.depth > 64 else 0.5
                    power_score  += 0.7
                elif style == "DSP":
                    speed_score  += 1.0 if m.kind in (ModuleKind.MUL, ModuleKind.DSP48E1) else 0.4
                    area_score   += 0.9
                    power_score  += 0.6
            n = max(len(self._modules), 1)
            return (speed_score / n) * 0.6 + (power_score / n) * 0.2 + (area_score / n) * 0.2

        population = [rand_chromosome() for _ in range(pop_size)]
        best_chrom = max(population, key=fitness)
        initial_fit = fitness(best_chrom)

        for _ in range(generations):
            population.sort(key=fitness, reverse=True)
            next_gen = population[:5]  # елітизм
            while len(next_gen) < pop_size:
                p1 = random.choice(population[:20])
                p2 = random.choice(population[:20])
                cut = random.randint(1, max(len(p1) - 1, 1))
                child = p1[:cut] + p2[cut:]
                if random.random() < mut_rate:
                    idx = random.randint(0, len(child) - 1)
                    child[idx] = random.choice(styles)
                next_gen.append(child)
            population = next_gen

        best_chrom = max(population, key=fitness)
        final_fit  = fitness(best_chrom)
        improvement = ((final_fit - initial_fit) / max(initial_fit, 1e-9)) * 100.0

        config = {
            m.name: best_chrom[i]
            for i, m in enumerate(self._modules)
        }
        return config, improvement

    def _build_report(
        self, profile: DeviceProfile, config: dict, improvement: float
    ) -> str:
        lines = [
            "═══ Звіт оптимізації Spartan-7 ═══",
            f"Пристрій:  {self.device_id}",
            f"Тактова:   {self.clock_mhz} МГц",
            f"Модулів:   {len(self._modules)}",
            f"Покращення ГА: {improvement:.1f}%",
            "",
            "── Конфігурація модулів ──",
        ]
        for name, style in config.items():
            lines.append(f"  {name:20s} → {style}")
        lines += [
            "",
            "── Ресурси пристрою ──",
            f"  LUTs:  {profile.lut_total}",
            f"  FFs:   {profile.ff_total}",
            f"  BRAMs: {profile.bram_total}",
            f"  DSPs:  {profile.dsp_total}",
        ]
        return "\n".join(lines)


# ──────────────────────────────────────────────
#  CLI-точка входу (викликається з C++)
# ──────────────────────────────────────────────
def _cli():
    parser = argparse.ArgumentParser(description="Spartan-7 Optimizer CLI")
    parser.add_argument("--mode",   choices=["generate", "analyze", "full"], default="full")
    parser.add_argument("--device", default="xc7s50-1fgg484")
    parser.add_argument("--clock",  type=float, default=100.0)
    parser.add_argument("--work-dir", default="build")
    args = parser.parse_args()

    opt = Spartan7Optimizer(args.device, args.clock, args.work_dir)

    if args.mode == "analyze":
        profile = opt._analyzer.get_static_profile(args.device)
        print(json.dumps(profile.__dict__, indent=2))
    elif args.mode in ("generate", "full"):
        result = opt.compile()
        print(result.report)


if __name__ == "__main__":
    _cli()
