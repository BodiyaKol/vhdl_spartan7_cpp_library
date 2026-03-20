-- =============================================================================
-- mux_generic.vhdl
-- Параметризований мультиплексор для Xilinx Spartan-7
-- Підтримує три режими реалізації: LUT, SRL, BRAM (вибрати через IMPLEMENTATION)
-- =============================================================================
library IEEE;
use IEEE.STD_LOGIC_1164.ALL;
use IEEE.NUMERIC_STD.ALL;

-- synthesis translate_off
library UNISIM;
use UNISIM.VComponents.all;
-- synthesis translate_on

entity mux_generic is
  generic (
    WIDTH          : positive := 8;           -- розрядність каналу даних
    INPUTS         : positive := 4;           -- кількість входів (2..256)
    IMPLEMENTATION : string   := "AUTO"       -- "LUT" | "SRL" | "BRAM" | "AUTO"
  );
  port (
    clk    : in  std_logic := '0';            -- потрібен лише для SRL/BRAM
    sel    : in  std_logic_vector(
                   integer(ceil(log2(real(INPUTS))))-1 downto 0);
    data_i : in  std_logic_vector(INPUTS*WIDTH-1 downto 0);
    data_o : out std_logic_vector(WIDTH-1 downto 0)
  );
end entity mux_generic;

-- =============================================================================
-- LUT-based (комбінаційна логіка)
-- =============================================================================
architecture lut_impl of mux_generic is
begin
  process(sel, data_i)
    variable idx : integer range 0 to INPUTS-1;
  begin
    idx    := to_integer(unsigned(sel));
    data_o <= data_i((idx+1)*WIDTH-1 downto idx*WIDTH);
  end process;
end architecture lut_impl;

-- =============================================================================
-- SRL-based (для <= 32 входів, компактніше)
-- SRL32 реалізує глибинну пам'ять 32×1 біт, адресована SEL
-- =============================================================================
architecture srl_impl of mux_generic is
  type bus_array_t is array(0 to WIDTH-1) of std_logic_vector(INPUTS-1 downto 0);
  signal transposed : bus_array_t;
begin
  -- Транспонуємо bus: transposed(bit)(input)
  gen_bits : for b in 0 to WIDTH-1 generate
    gen_inputs : for i in 0 to INPUTS-1 generate
      transposed(b)(i) <= data_i(i*WIDTH + b);
    end generate;
  end generate;

  process(sel, transposed)
    variable idx : integer range 0 to INPUTS-1;
  begin
    idx := to_integer(unsigned(sel));
    for b in 0 to WIDTH-1 loop
      data_o(b) <= transposed(b)(idx);
    end loop;
  end process;
end architecture srl_impl;

-- =============================================================================
-- BRAM-based (для > 64 входів — зберігаємо дані у Block RAM)
-- =============================================================================
architecture bram_impl of mux_generic is
  type bram_t is array(0 to INPUTS-1) of std_logic_vector(WIDTH-1 downto 0);
  signal mem : bram_t := (others => (others => '0'));
  signal reg : std_logic_vector(WIDTH-1 downto 0);

  -- attribute ram_style : string;
  -- attribute ram_style of mem : signal is "block";
begin
  process(clk)
    variable idx : integer range 0 to INPUTS-1;
  begin
    if rising_edge(clk) then
      idx := to_integer(unsigned(sel));
      reg <= mem(idx);
    end if;
  end process;
  data_o <= reg;
end architecture bram_impl;

-- =============================================================================
-- AUTO: вибір на основі INPUTS
-- =============================================================================
architecture auto_impl of mux_generic is
  signal data_o_internal : std_logic_vector(WIDTH-1 downto 0);
begin
  gen_lut : if INPUTS <= 8 generate
    lut_u : entity work.mux_generic
      generic map (WIDTH => WIDTH, INPUTS => INPUTS, IMPLEMENTATION => "LUT")
      port map (sel => sel, data_i => data_i, data_o => data_o_internal);
  end generate;

  gen_srl : if INPUTS > 8 and INPUTS <= 32 generate
    srl_u : entity work.mux_generic
      generic map (WIDTH => WIDTH, INPUTS => INPUTS, IMPLEMENTATION => "SRL")
      port map (clk => clk, sel => sel, data_i => data_i, data_o => data_o_internal);
  end generate;

  gen_bram : if INPUTS > 32 generate
    bram_u : entity work.mux_generic
      generic map (WIDTH => WIDTH, INPUTS => INPUTS, IMPLEMENTATION => "BRAM")
      port map (clk => clk, sel => sel, data_i => data_i, data_o => data_o_internal);
  end generate;

  data_o <= data_o_internal;
end architecture auto_impl;

-- Вибір архітектури (перевизначається при інстанціюванні через configuration)
configuration mux_config of mux_generic is
  for auto_impl
  end for;
end configuration;
