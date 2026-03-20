-- =============================================================================
-- memory_controller.vhdl
-- Контролер розподіленої та блочної пам'яті Spartan-7
-- Підтримує: True Dual-Port BRAM, Simple Dual-Port, Distributed RAM, FIFO
-- =============================================================================
library IEEE;
use IEEE.STD_LOGIC_1164.ALL;
use IEEE.NUMERIC_STD.ALL;

-- =============================================================================
-- Package з типами
-- =============================================================================
package mem_pkg is
  type mem_mode_t is (SIMPLE_DUAL_PORT, TRUE_DUAL_PORT, SINGLE_PORT);
  type mem_type_t is (BRAM_BLOCK, DISTRIBUTED, ULTRA);  -- ULTRA для UltraScale
end package;

-- =============================================================================
-- Simple Dual-Port Block RAM
-- Port A: write, Port B: read (незалежні тактові)
-- =============================================================================
library IEEE;
use IEEE.STD_LOGIC_1164.ALL;
use IEEE.NUMERIC_STD.ALL;

entity bram_sdp is
  generic (
    DATA_WIDTH  : positive := 8;
    ADDR_WIDTH  : positive := 10;          -- 2^ADDR_WIDTH слів
    OUTPUT_REG  : boolean  := true;        -- додатковий регістр виходу
    INIT_VALUE  : std_logic := '0'
  );
  port (
    -- Write port (Port A)
    clk_a   : in  std_logic;
    en_a    : in  std_logic := '1';
    we_a    : in  std_logic;
    addr_a  : in  std_logic_vector(ADDR_WIDTH-1 downto 0);
    din_a   : in  std_logic_vector(DATA_WIDTH-1 downto 0);

    -- Read port (Port B)
    clk_b   : in  std_logic;
    en_b    : in  std_logic := '1';
    addr_b  : in  std_logic_vector(ADDR_WIDTH-1 downto 0);
    dout_b  : out std_logic_vector(DATA_WIDTH-1 downto 0)
  );
end entity bram_sdp;

architecture rtl of bram_sdp is
  constant DEPTH : positive := 2**ADDR_WIDTH;
  type ram_t is array(0 to DEPTH-1) of std_logic_vector(DATA_WIDTH-1 downto 0);
  shared variable mem : ram_t := (others => (others => INIT_VALUE));

  signal dout_raw : std_logic_vector(DATA_WIDTH-1 downto 0);

  -- Xilinx synthesis directive — використовувати BRAM примітиви
  attribute ram_style : string;
  attribute ram_style of mem : variable is "block";
begin
  -- Write port
  process(clk_a)
  begin
    if rising_edge(clk_a) then
      if en_a = '1' and we_a = '1' then
        mem(to_integer(unsigned(addr_a))) := din_a;
      end if;
    end if;
  end process;

  -- Read port без реєстра
  process(clk_b)
  begin
    if rising_edge(clk_b) then
      if en_b = '1' then
        dout_raw <= mem(to_integer(unsigned(addr_b)));
      end if;
    end if;
  end process;

  -- Опціональний output register (покращує timing)
  gen_outreg : if OUTPUT_REG generate
    process(clk_b)
    begin
      if rising_edge(clk_b) then
        dout_b <= dout_raw;
      end if;
    end process;
  end generate;

  gen_no_outreg : if not OUTPUT_REG generate
    dout_b <= dout_raw;
  end generate;
end architecture rtl;

-- =============================================================================
-- Розподілена RAM (до 64 слів, без BRAM примітивів)
-- =============================================================================
library IEEE;
use IEEE.STD_LOGIC_1164.ALL;
use IEEE.NUMERIC_STD.ALL;

entity dist_ram is
  generic (
    DATA_WIDTH : positive := 8;
    ADDR_WIDTH : positive := 6    -- до 64 слів
  );
  port (
    clk    : in  std_logic;
    we     : in  std_logic;
    addr_w : in  std_logic_vector(ADDR_WIDTH-1 downto 0);
    addr_r : in  std_logic_vector(ADDR_WIDTH-1 downto 0);
    din    : in  std_logic_vector(DATA_WIDTH-1 downto 0);
    dout   : out std_logic_vector(DATA_WIDTH-1 downto 0)
  );
end entity dist_ram;

architecture rtl of dist_ram is
  constant DEPTH : positive := 2**ADDR_WIDTH;
  type ram_t is array(0 to DEPTH-1) of std_logic_vector(DATA_WIDTH-1 downto 0);
  signal mem : ram_t := (others => (others => '0'));

  attribute ram_style : string;
  attribute ram_style of mem : signal is "distributed";
begin
  process(clk)
  begin
    if rising_edge(clk) then
      if we = '1' then
        mem(to_integer(unsigned(addr_w))) <= din;
      end if;
    end if;
  end process;
  -- Асинхронний read (характеристика distributed RAM)
  dout <= mem(to_integer(unsigned(addr_r)));
end architecture rtl;

-- =============================================================================
-- FIFO — побудований на BRAM з head/tail покажчиками
-- =============================================================================
library IEEE;
use IEEE.STD_LOGIC_1164.ALL;
use IEEE.NUMERIC_STD.ALL;

entity fifo_bram is
  generic (
    DATA_WIDTH : positive := 8;
    DEPTH      : positive := 512;    -- кількість слів
    FWFT_MODE  : boolean  := true    -- First-Word-Fall-Through
  );
  port (
    clk     : in  std_logic;
    rst     : in  std_logic;
    wr_en   : in  std_logic;
    rd_en   : in  std_logic;
    din     : in  std_logic_vector(DATA_WIDTH-1 downto 0);
    dout    : out std_logic_vector(DATA_WIDTH-1 downto 0);
    full    : out std_logic;
    empty   : out std_logic;
    almost_full  : out std_logic;  -- fill >= DEPTH-4
    almost_empty : out std_logic;  -- fill <= 3
    fill_level   : out std_logic_vector(
                         integer(ceil(log2(real(DEPTH+1))))-1 downto 0)
  );
end entity fifo_bram;

architecture rtl of fifo_bram is
  constant ADDR_W : positive := integer(ceil(log2(real(DEPTH))));
  constant FILL_W : positive := integer(ceil(log2(real(DEPTH+1))));

  type ram_t is array(0 to DEPTH-1) of std_logic_vector(DATA_WIDTH-1 downto 0);
  signal mem : ram_t := (others => (others => '0'));

  attribute ram_style : string;
  attribute ram_style of mem : signal is "block";

  signal wr_ptr  : unsigned(ADDR_W-1 downto 0) := (others => '0');
  signal rd_ptr  : unsigned(ADDR_W-1 downto 0) := (others => '0');
  signal fill    : unsigned(FILL_W-1 downto 0)  := (others => '0');
  signal dout_r  : std_logic_vector(DATA_WIDTH-1 downto 0);
begin

  full        <= '1' when fill = DEPTH    else '0';
  empty       <= '1' when fill = 0        else '0';
  almost_full <= '1' when fill >= DEPTH-4 else '0';
  almost_empty<= '1' when fill <= 3       else '0';
  fill_level  <= std_logic_vector(fill);

  -- Write path
  process(clk)
  begin
    if rising_edge(clk) then
      if rst = '1' then
        wr_ptr <= (others => '0');
      elsif wr_en = '1' and fill < DEPTH then
        mem(to_integer(wr_ptr)) <= din;
        wr_ptr <= wr_ptr + 1;
      end if;
    end if;
  end process;

  -- Read path + fill counter
  process(clk)
  begin
    if rising_edge(clk) then
      if rst = '1' then
        rd_ptr <= (others => '0');
        fill   <= (others => '0');
      else
        -- fill counter
        if    (wr_en = '1' and fill < DEPTH) and not (rd_en = '1' and fill > 0) then
          fill <= fill + 1;
        elsif (rd_en = '1' and fill > 0)     and not (wr_en = '1' and fill < DEPTH) then
          fill <= fill - 1;
        end if;
        -- read
        if rd_en = '1' and fill > 0 then
          dout_r <= mem(to_integer(rd_ptr));
          rd_ptr <= rd_ptr + 1;
        end if;
      end if;
    end if;
  end process;

  -- FWFT
  gen_fwft : if FWFT_MODE generate
    dout <= mem(to_integer(rd_ptr)) when fill > 0 else (others => '0');
  end generate;
  gen_std : if not FWFT_MODE generate
    dout <= dout_r;
  end generate;

end architecture rtl;
