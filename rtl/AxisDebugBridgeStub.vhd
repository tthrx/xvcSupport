-------------------------------------------------------------------------------
-- Title      : JTAG Support
-------------------------------------------------------------------------------
-- File       : AxisDebugBridgeStub.vhd
-- Author     : Till Straumann <strauman@slac.stanford.edu>
-- Company    : SLAC National Accelerator Laboratory
-- Created    : 2017-12-05
-- Last update: 2017-12-05
-- Platform   :
-- Standard   : VHDL'93/02
-------------------------------------------------------------------------------
-- Description:
-------------------------------------------------------------------------------
-- This file is part of 'SLAC Firmware Standard Library'.
-- It is subject to the license terms in the LICENSE.txt file found in the
-- top-level directory of this distribution and at:
--    https://confluence.slac.stanford.edu/display/ppareg/LICENSE.html.
-- No part of 'SLAC Firmware Standard Library', including this file,
-- may be copied, modified, propagated, or distributed except according to
-- the terms contained in the LICENSE.txt file.
-------------------------------------------------------------------------------

-- Axi Stream to JTAG Protocol

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

use work.StdRtlPkg.all;
use work.AxiStreamPkg.all;
use work.AxisToJtagPkg.all;

architecture AxisDebugBridgeStub of AxisDebugBridge is

   type StateType is (READY, SKIP, REPLY);

   type RegType is record
      state    : StateType;
      repValid : sl;
      repData  : slv(31 downto 0);
      reqReady : sl;
   end record RegType;

   constant REG_INIT_C : RegType := (
      state    => READY,
      repValid => '0',
      repData  => (others => '0'),
      reqReady => '1'
   );

   signal mReply : AxiStreamMasterType := AXI_STREAM_MASTER_INIT_C;
   signal sReq   : AxiStreamSlaveType  := AXI_STREAM_SLAVE_INIT_C;

   signal r      : RegType             := REG_INIT_C;
   signal rin    : RegType;

begin

   sReq.tReady               <= r.reqReady;
   mReply.tValid             <= r.repValid;
   mReply.tLast              <= '1';
   mReply.tData(31 downto 0) <= r.repData;

   sAxisReq                  <= sReq;
   mAxisTdo                  <= mReply;

   U_COMB : process(r, mAxisReq, sAxisTdo) is
      variable v : RegType;
   begin
      v := r;

      case (r.state) is

         when READY =>
            if ( mAxisReq.tValid = '1' ) then
               v.repData := ( others => '0' );
               if ( getVersion( mAxisReq.tData ) /= PRO_VERSN_C ) then
                  setVersion( PRO_VERSN_C      , v.repData );
                  setErr    ( ERR_BAD_VERSION_C, v.repData );
               elsif ( getCommand( mAxisReq.tData ) /= CMD_QUERY_C ) then
                  setErr    ( ERR_BAD_COMMAND_C, v.repData );
               else
                  setErr    ( ERR_NOT_PRESENT_C, v.repData );
               end if;
               if ( mAxisReq.tLast = '1' ) then
                  v.reqReady := '0';
               end if;
               v.repValid := '1';
               v.state    := REPLY;
            end if;

         when REPLY =>
            if ( (mAxisReq.tValid and mAxisReq.tLast and r.reqReady) = '1' ) then
               v.reqReady := '0';
            end if;
            if ( sAxisTdo.tReady = '1' ) then
               v.repValid := '0';
               if ( v.reqReady = '1' ) then
                  -- no TLAST seen yet
                  v.state := SKIP;
               else
                  v.reqReady := '1';
                  v.state    := READY;
               end if;
            end if;

         when SKIP =>
            if ( (mAxisReq.tValid and mAxisReq.tLast) = '1' ) then
               v.state := READY;
            end if;

      end case;

      rin <= v;

   end process U_comb;

   U_SEQ : process( axisClk ) is
   begin
      if ( rising_edge( axisClk ) ) then
         if ( axisRst /= '0' ) then
            r <= REG_INIT_C after TPD_G;
         else
            r <= rin after TPD_G;
         end if;
      end if;
   end process U_seq;

end architecture AxisDebugBridgeStub;
