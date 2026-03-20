-- =============================================================================
-- wrapper_template.vhdl
-- AXI4-Lite обгортка для інтеграції C++ модулів у систему Spartan-7
-- Реалізує стандартний slave-інтерфейс для Xilinx Vivado IP Integrator
-- =============================================================================
library IEEE;
use IEEE.STD_LOGIC_1164.ALL;
use IEEE.NUMERIC_STD.ALL;

-- =============================================================================
-- AXI4-Lite Slave Wrapper
-- Надає 16 регістрів 32-bit доступних через AXI4-Lite
-- =============================================================================
entity axi4lite_wrapper is
  generic (
    C_S_AXI_DATA_WIDTH : integer := 32;
    C_S_AXI_ADDR_WIDTH : integer := 6    -- 16 регістрів × 4 байти = 64 адреси
  );
  port (
    -- AXI4-Lite Slave Interface
    S_AXI_ACLK    : in  std_logic;
    S_AXI_ARESETN : in  std_logic;       -- активний LOW

    -- Write address channel
    S_AXI_AWADDR  : in  std_logic_vector(C_S_AXI_ADDR_WIDTH-1 downto 0);
    S_AXI_AWPROT  : in  std_logic_vector(2 downto 0);
    S_AXI_AWVALID : in  std_logic;
    S_AXI_AWREADY : out std_logic;

    -- Write data channel
    S_AXI_WDATA   : in  std_logic_vector(C_S_AXI_DATA_WIDTH-1 downto 0);
    S_AXI_WSTRB   : in  std_logic_vector((C_S_AXI_DATA_WIDTH/8)-1 downto 0);
    S_AXI_WVALID  : in  std_logic;
    S_AXI_WREADY  : out std_logic;

    -- Write response channel
    S_AXI_BRESP   : out std_logic_vector(1 downto 0);
    S_AXI_BVALID  : out std_logic;
    S_AXI_BREADY  : in  std_logic;

    -- Read address channel
    S_AXI_ARADDR  : in  std_logic_vector(C_S_AXI_ADDR_WIDTH-1 downto 0);
    S_AXI_ARPROT  : in  std_logic_vector(2 downto 0);
    S_AXI_ARVALID : in  std_logic;
    S_AXI_ARREADY : out std_logic;

    -- Read data channel
    S_AXI_RDATA   : out std_logic_vector(C_S_AXI_DATA_WIDTH-1 downto 0);
    S_AXI_RRESP   : out std_logic_vector(1 downto 0);
    S_AXI_RVALID  : out std_logic;
    S_AXI_RREADY  : in  std_logic;

    -- User logic ports (приєднуються до користувацького IP)
    user_reg_wr    : out std_logic_vector(C_S_AXI_DATA_WIDTH-1 downto 0);
    user_reg_rd    : in  std_logic_vector(C_S_AXI_DATA_WIDTH-1 downto 0) := (others => '0');
    user_reg_sel   : out std_logic_vector(3 downto 0);   -- індекс регістра
    user_wr_en     : out std_logic;
    user_rd_en     : out std_logic
  );
end entity axi4lite_wrapper;

architecture rtl of axi4lite_wrapper is
  constant ADDR_LSB  : integer := 2;  -- log2(C_S_AXI_DATA_WIDTH/8)
  constant OPT_MEM_ADDR_BITS : integer := 3;  -- 4 регістри

  -- Internal signals
  signal axi_awready : std_logic := '0';
  signal axi_wready  : std_logic := '0';
  signal axi_bresp   : std_logic_vector(1 downto 0) := "00";
  signal axi_bvalid  : std_logic := '0';
  signal axi_arready : std_logic := '0';
  signal axi_rdata   : std_logic_vector(C_S_AXI_DATA_WIDTH-1 downto 0) := (others => '0');
  signal axi_rresp   : std_logic_vector(1 downto 0) := "00";
  signal axi_rvalid  : std_logic := '0';

  signal aw_en       : std_logic := '1';
  signal axi_awaddr  : std_logic_vector(C_S_AXI_ADDR_WIDTH-1 downto 0);
  signal axi_araddr  : std_logic_vector(C_S_AXI_ADDR_WIDTH-1 downto 0);

  -- Регістри користувача (REG0..REG15)
  type reg_array_t is array(0 to 15) of std_logic_vector(C_S_AXI_DATA_WIDTH-1 downto 0);
  signal slv_reg : reg_array_t := (others => (others => '0'));

  signal slv_reg_rden : std_logic;
  signal slv_reg_wren : std_logic;
  signal reg_data_out : std_logic_vector(C_S_AXI_DATA_WIDTH-1 downto 0);
  signal byte_index   : integer range 0 to C_S_AXI_DATA_WIDTH/8-1;
begin
  -- ── Підключення виходів ─────────────────
  S_AXI_AWREADY <= axi_awready;
  S_AXI_WREADY  <= axi_wready;
  S_AXI_BRESP   <= axi_bresp;
  S_AXI_BVALID  <= axi_bvalid;
  S_AXI_ARREADY <= axi_arready;
  S_AXI_RDATA   <= axi_rdata;
  S_AXI_RRESP   <= axi_rresp;
  S_AXI_RVALID  <= axi_rvalid;

  -- ── Write address ready ─────────────────
  process(S_AXI_ACLK)
  begin
    if rising_edge(S_AXI_ACLK) then
      if S_AXI_ARESETN = '0' then
        axi_awready <= '0';
        aw_en       <= '1';
      else
        if axi_awready = '0' and S_AXI_AWVALID = '1'
           and S_AXI_WVALID = '1' and aw_en = '1' then
          axi_awready <= '1';
          aw_en       <= '0';
        elsif S_AXI_BREADY = '1' and axi_bvalid = '1' then
          aw_en       <= '1';
          axi_awready <= '0';
        else
          axi_awready <= '0';
        end if;
      end if;
    end if;
  end process;

  -- ── Write address latch ──────────────────
  process(S_AXI_ACLK)
  begin
    if rising_edge(S_AXI_ACLK) then
      if S_AXI_ARESETN = '0' then
        axi_awaddr <= (others => '0');
      elsif axi_awready = '0' and S_AXI_AWVALID = '1'
            and S_AXI_WVALID = '1' and aw_en = '1' then
        axi_awaddr <= S_AXI_AWADDR;
      end if;
    end if;
  end process;

  -- ── Write data ready ─────────────────────
  process(S_AXI_ACLK)
  begin
    if rising_edge(S_AXI_ACLK) then
      if S_AXI_ARESETN = '0' then
        axi_wready <= '0';
      else
        if axi_wready = '0' and S_AXI_WVALID = '1'
           and S_AXI_AWVALID = '1' and aw_en = '1' then
          axi_wready <= '1';
        else
          axi_wready <= '0';
        end if;
      end if;
    end if;
  end process;

  -- ── Write to registers ───────────────────
  slv_reg_wren <= axi_wready and S_AXI_WVALID and axi_awready and S_AXI_AWVALID;

  process(S_AXI_ACLK)
    variable loc_addr : std_logic_vector(OPT_MEM_ADDR_BITS-1 downto 0);
  begin
    if rising_edge(S_AXI_ACLK) then
      if S_AXI_ARESETN = '0' then
        for i in 0 to 15 loop
          slv_reg(i) <= (others => '0');
        end loop;
      elsif slv_reg_wren = '1' then
        loc_addr := axi_awaddr(ADDR_LSB + OPT_MEM_ADDR_BITS - 1 downto ADDR_LSB);
        for byte_index in 0 to (C_S_AXI_DATA_WIDTH/8-1) loop
          if S_AXI_WSTRB(byte_index) = '1' then
            slv_reg(to_integer(unsigned(loc_addr)))(
              byte_index*8+7 downto byte_index*8
            ) <= S_AXI_WDATA(byte_index*8+7 downto byte_index*8);
          end if;
        end loop;
      end if;
    end if;
  end process;

  -- ── Write response ───────────────────────
  process(S_AXI_ACLK)
  begin
    if rising_edge(S_AXI_ACLK) then
      if S_AXI_ARESETN = '0' then
        axi_bvalid <= '0';
        axi_bresp  <= "00";
      elsif axi_awready = '1' and S_AXI_AWVALID = '1'
            and axi_wready = '1' and S_AXI_WVALID = '1'
            and axi_bvalid = '0' then
        axi_bvalid <= '1';
        axi_bresp  <= "00";    -- OKAY
      elsif S_AXI_BREADY = '1' and axi_bvalid = '1' then
        axi_bvalid <= '0';
      end if;
    end if;
  end process;

  -- ── Read address ready ───────────────────
  process(S_AXI_ACLK)
  begin
    if rising_edge(S_AXI_ACLK) then
      if S_AXI_ARESETN = '0' then
        axi_arready <= '0';
        axi_araddr  <= (others => '1');
      else
        if axi_arready = '0' and S_AXI_ARVALID = '1' then
          axi_arready <= '1';
          axi_araddr  <= S_AXI_ARADDR;
        else
          axi_arready <= '0';
        end if;
      end if;
    end if;
  end process;

  -- ── Read data valid ─────────────────────
  process(S_AXI_ACLK)
  begin
    if rising_edge(S_AXI_ACLK) then
      if S_AXI_ARESETN = '0' then
        axi_rvalid <= '0';
        axi_rresp  <= "00";
      elsif axi_arready = '1' and S_AXI_ARVALID = '1'
            and axi_rvalid = '0' then
        axi_rvalid <= '1';
        axi_rresp  <= "00";
      elsif axi_rvalid = '1' and S_AXI_RREADY = '1' then
        axi_rvalid <= '0';
      end if;
    end if;
  end process;

  -- ── Read mux ────────────────────────────
  slv_reg_rden <= axi_arready and S_AXI_ARVALID and (not axi_rvalid);

  process(slv_reg, axi_araddr, user_reg_rd)
    variable loc_addr : std_logic_vector(OPT_MEM_ADDR_BITS-1 downto 0);
  begin
    loc_addr    := axi_araddr(ADDR_LSB + OPT_MEM_ADDR_BITS - 1 downto ADDR_LSB);
    -- REG0 відображає user_reg_rd (статус від user logic)
    if to_integer(unsigned(loc_addr)) = 0 then
      reg_data_out <= user_reg_rd;
    else
      reg_data_out <= slv_reg(to_integer(unsigned(loc_addr)));
    end if;
  end process;

  process(S_AXI_ACLK)
  begin
    if rising_edge(S_AXI_ACLK) then
      if S_AXI_ARESETN = '0' then
        axi_rdata <= (others => '0');
      elsif slv_reg_rden = '1' then
        axi_rdata <= reg_data_out;
      end if;
    end if;
  end process;

  -- ── User logic connections ───────────────
  user_reg_wr  <= slv_reg(1);   -- REG1: control від PS -> user logic
  user_reg_sel <= slv_reg(1)(3 downto 0);
  user_wr_en   <= slv_reg_wren;
  user_rd_en   <= slv_reg_rden;

end architecture rtl;
