-- =============================================================================
-- demux_generic.vhdl
-- Параметризований демультиплексор для Xilinx Spartan-7
-- =============================================================================
library IEEE;
use IEEE.STD_LOGIC_1164.ALL;
use IEEE.NUMERIC_STD.ALL;

entity demux_generic is
  generic (
    WIDTH          : positive := 8;     -- розрядність каналу даних
    OUTPUTS        : positive := 4;     -- кількість виходів (2..256)
    REGISTERED     : boolean  := false  -- true = зареєстрований вихід (додатковий FF)
  );
  port (
    clk    : in  std_logic := '0';      -- потрібен лише при REGISTERED=true
    rst    : in  std_logic := '0';
    sel    : in  std_logic_vector(
                   integer(ceil(log2(real(OUTPUTS))))-1 downto 0);
    data_i : in  std_logic_vector(WIDTH-1 downto 0);
    data_o : out std_logic_vector(OUTPUTS*WIDTH-1 downto 0)
  );
end entity demux_generic;

-- =============================================================================
-- Комбінаційна реалізація
-- =============================================================================
architecture comb of demux_generic is
begin
  process(sel, data_i)
    variable idx : integer range 0 to OUTPUTS-1;
  begin
    data_o <= (others => '0');
    idx    := to_integer(unsigned(sel));
    data_o((idx+1)*WIDTH-1 downto idx*WIDTH) <= data_i;
  end process;
end architecture comb;

-- =============================================================================
-- Зареєстрована реалізація (register on output)
-- =============================================================================
architecture registered of demux_generic is
  signal data_o_comb : std_logic_vector(OUTPUTS*WIDTH-1 downto 0);
begin
  -- Комбінаційна частина
  process(sel, data_i)
    variable idx : integer range 0 to OUTPUTS-1;
  begin
    data_o_comb <= (others => '0');
    idx         := to_integer(unsigned(sel));
    data_o_comb((idx+1)*WIDTH-1 downto idx*WIDTH) <= data_i;
  end process;

  -- Реєстрація виходу
  process(clk)
  begin
    if rising_edge(clk) then
      if rst = '1' then
        data_o <= (others => '0');
      else
        data_o <= data_o_comb;
      end if;
    end if;
  end process;
end architecture registered;

-- =============================================================================
-- Вибір через generate (без конфігурацій для простоти інстанціювання)
-- =============================================================================
architecture rtl of demux_generic is
begin
  gen_comb : if not REGISTERED generate
    process(sel, data_i)
      variable idx : integer range 0 to OUTPUTS-1;
    begin
      data_o <= (others => '0');
      idx    := to_integer(unsigned(sel));
      data_o((idx+1)*WIDTH-1 downto idx*WIDTH) <= data_i;
    end process;
  end generate;

  gen_reg : if REGISTERED generate
    signal pipeline_reg : std_logic_vector(OUTPUTS*WIDTH-1 downto 0)
                          := (others => '0');
  begin
    process(clk)
      variable idx : integer range 0 to OUTPUTS-1;
    begin
      if rising_edge(clk) then
        if rst = '1' then
          pipeline_reg <= (others => '0');
        else
          pipeline_reg <= (others => '0');
          idx          := to_integer(unsigned(sel));
          pipeline_reg((idx+1)*WIDTH-1 downto idx*WIDTH) <= data_i;
        end if;
      end if;
    end process;
    data_o <= pipeline_reg;
  end generate;
end architecture rtl;
