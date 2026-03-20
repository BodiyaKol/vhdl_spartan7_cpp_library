"""
hardware_analyzer.py
Профайлінг Spartan-7: сканування доступних ресурсів,
вимір timing-шляхів, аналіз routing congestion.
Якщо Vivado недоступний — повертає статичні дані для xc7s50 тощо.
"""

from __future__ import annotations

import json
import logging
from dataclasses import dataclass, field, asdict
from pathlib import Path
from typing import Optional, TYPE_CHECKING

if TYPE_CHECKING:
    from vivado_bridge import VivadoBridge

log = logging.getLogger(__name__)

# ──────────────────────────────────────────────
#  Статичні характеристики Spartan-7
# ──────────────────────────────────────────────
_STATIC_PROFILES: dict[str, dict] = {
    "xc7s6": dict(
        lut_total=3750, ff_total=7500, bram_total=5,
        dsp_total=10, io_total=100,
        clock_regions=1, max_clock_mhz=450.0,
    ),
    "xc7s15": dict(
        lut_total=8000, ff_total=16000, bram_total=20,
        dsp_total=40, io_total=100,
        clock_regions=1, max_clock_mhz=450.0,
    ),
    "xc7s25": dict(
        lut_total=14600, ff_total=29200, bram_total=45,
        dsp_total=80, io_total=150,
        clock_regions=2, max_clock_mhz=450.0,
    ),
    "xc7s50": dict(
        lut_total=32600, ff_total=65200, bram_total=75,
        dsp_total=120, io_total=250,
        clock_regions=3, max_clock_mhz=450.0,
    ),
    "xc7s75": dict(
        lut_total=47000, ff_total=94000, bram_total=135,
        dsp_total=150, io_total=300,
        clock_regions=4, max_clock_mhz=450.0,
    ),
    "xc7s100": dict(
        lut_total=64000, ff_total=128000, bram_total=200,
        dsp_total=160, io_total=400,
        clock_regions=5, max_clock_mhz=450.0,
    ),
}


# ──────────────────────────────────────────────
#  Профіль пристрою
# ──────────────────────────────────────────────
@dataclass
class TimingCharacteristics:
    lut_delay_ns:          float = 0.60   # затримка одного рівня LUT
    routing_per_mm_ns:     float = 0.05   # routing затримка на мм відстані
    bram_read_latency_ns:  float = 2.00   # read latency BRAM
    dsp_pipeline_ns:       float = 3.80   # DSP48E1 pipeline
    ff_setup_ns:           float = 0.10
    ff_hold_ns:            float = 0.05
    clock_skew_ns:         float = 0.20


@dataclass
class RoutingInfo:
    average_congestion_pct: float = 15.0  # % заповненості routing ресурсів
    hotspot_regions:        list[str] = field(default_factory=list)
    estimated_wirelength_mm: float = 0.0


@dataclass
class DeviceProfile:
    device_id:      str   = ""
    lut_total:      int   = 0
    ff_total:       int   = 0
    bram_total:     int   = 0
    dsp_total:      int   = 0
    io_total:       int   = 0
    clock_regions:  int   = 1
    max_clock_mhz:  float = 450.0
    source:         str   = "static"  # "static" | "vivado"

    timing: TimingCharacteristics = field(default_factory=TimingCharacteristics)
    routing: RoutingInfo          = field(default_factory=RoutingInfo)

    def to_json(self) -> str:
        return json.dumps(asdict(self), indent=2)

    @classmethod
    def from_json(cls, text: str) -> "DeviceProfile":
        d = json.loads(text)
        profile = cls(**{k: v for k, v in d.items()
                         if k not in ("timing", "routing")})
        if "timing" in d:
            profile.timing = TimingCharacteristics(**d["timing"])
        if "routing" in d:
            profile.routing = RoutingInfo(**d["routing"])
        return profile


# ──────────────────────────────────────────────
#  Аналізатор апаратури
# ──────────────────────────────────────────────
class HardwareAnalyzer:
    """
    Отримує характеристики Spartan-7:
    - через Vivado (якщо доступний)
    - через статичну таблицю (fallback)
    """

    CACHE_FILE = "build/device_profile_cache.json"

    def __init__(self, bridge: "VivadoBridge"):
        self._bridge = bridge
        self._cache: dict[str, DeviceProfile] = {}
        self._load_cache()

    # ── Головний метод ──────────────────────
    def scan(self, device_id: str) -> DeviceProfile:
        """
        Повне сканування через Vivado.
        Кешує результат між запусками.
        """
        if device_id in self._cache:
            log.info("Використовую кешований профіль для %s", device_id)
            return self._cache[device_id]

        if not self._bridge.is_available():
            raise RuntimeError("Vivado недоступний")

        log.info("Сканую ресурси %s через Vivado...", device_id)
        util = self._bridge.run_device_analysis(device_id)
        if util is None:
            raise RuntimeError("Vivado не повернув даних")

        profile = self.get_static_profile(device_id)  # базові характеристики
        profile.source   = "vivado"
        # Оновити значення з Vivado-звіту
        if util.lut_total:  profile.lut_total  = util.lut_total
        if util.ff_total:   profile.ff_total   = util.ff_total
        if util.bram_total: profile.bram_total = util.bram_total
        if util.dsp_total:  profile.dsp_total  = util.dsp_total

        self._cache[device_id] = profile
        self._save_cache()
        return profile

    def get_static_profile(self, device_id: str) -> DeviceProfile:
        """Повертає статичний профіль без виклику Vivado."""
        base_id = next(
            (k for k in _STATIC_PROFILES if device_id.startswith(k)),
            None
        )
        if base_id is None:
            log.warning("Невідомий пристрій %s, використовую xc7s50", device_id)
            base_id = "xc7s50"

        data = _STATIC_PROFILES[base_id].copy()
        return DeviceProfile(device_id=device_id, source="static", **data)

    # ── Аналіз timing ────────────────────────
    def measure_timing_paths(
        self, device_id: str, num_paths: int = 10
    ) -> list[dict]:
        """
        Запускає report_timing через Vivado після синтезу.
        Повертає список критичних шляхів.
        """
        if not self._bridge.is_available():
            return self._synthetic_timing_paths(num_paths)

        tcl = f"""
open_checkpoint {{build/synth_output/synth.dcp}}
report_timing -max_paths {num_paths} -nworst 1 \\
    -file {{build/timing_paths.rpt}}
"""
        output = self._bridge._run_vivado_script(tcl)
        if output is None:
            return self._synthetic_timing_paths(num_paths)

        return self._parse_timing_paths(output)

    def _parse_timing_paths(self, text: str) -> list[dict]:
        import re
        paths = []
        for m in re.finditer(
            r"Slack\s*:\s*(-?[\d.]+)\s*ns.*?Source:\s*(\S+).*?Destination:\s*(\S+)",
            text, re.DOTALL
        ):
            paths.append({
                "slack_ns":    float(m.group(1)),
                "source":      m.group(2),
                "destination": m.group(3),
            })
        return paths[:10]

    def _synthetic_timing_paths(self, n: int) -> list[dict]:
        return [{"slack_ns": 1.5 - i * 0.1, "source": f"ff_{i}", "destination": f"lut_{i}"}
                for i in range(n)]

    # ── Аналіз routing ───────────────────────
    def analyze_routing_congestion(self) -> RoutingInfo:
        """Аналізує routing congestion після P&R."""
        if not self._bridge.is_available():
            return RoutingInfo()

        tcl = """
open_checkpoint {build/pnr_output/routed.dcp}
report_congestion -file {build/congestion.rpt}
"""
        output = self._bridge._run_vivado_script(tcl)
        if output is None:
            return RoutingInfo()

        import re
        ri = RoutingInfo()
        m = re.search(r"Average Congestion\s*:\s*([\d.]+)", output)
        if m:
            ri.average_congestion_pct = float(m.group(1))
        return ri

    # ── Кеш ─────────────────────────────────
    def _load_cache(self) -> None:
        path = Path(self.CACHE_FILE)
        if not path.exists():
            return
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
            for dev_id, raw in data.items():
                self._cache[dev_id] = DeviceProfile.from_json(json.dumps(raw))
        except Exception as exc:  # noqa: BLE001
            log.warning("Не вдалося завантажити кеш профілів: %s", exc)

    def _save_cache(self) -> None:
        path = Path(self.CACHE_FILE)
        path.parent.mkdir(parents=True, exist_ok=True)
        data = {k: json.loads(v.to_json()) for k, v in self._cache.items()}
        path.write_text(json.dumps(data, indent=2), encoding="utf-8")
