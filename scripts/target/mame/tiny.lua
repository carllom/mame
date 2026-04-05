-- license:BSD-3-Clause
-- copyright-holders:MAMEdev Team

---------------------------------------------------------------------------
--
--   tiny.lua
--
--   Small driver-specific example makefile
--   Use make SUBTARGET=tiny to build
--
---------------------------------------------------------------------------


--------------------------------------------------
-- Specify all the CPU cores necessary for the
-- drivers referenced in tiny.lst.
--------------------------------------------------

CPUS["M680X0"] = true -- e6400
CPUS["NS32000"] = true -- emu3
CPUS["M6800"] = true  -- RS232 -> votraxtnt machine
CPUS["Z80"] = true    -- RS232 -> heath_h19 -> tlb
CPUS["IE15"] = true   -- RS232 -> ie15 machine

--------------------------------------------------
-- Specify all the sound cores necessary for the
-- drivers referenced in tiny.lst.
--------------------------------------------------

SOUNDS["BEEP"] = true          -- RS232 -> heath_h19 -> tlb
SOUNDS["VOTRAX_SC01A"] = true  -- RS232 -> votraxtnt machine
SOUNDS["AY8910"] = true        -- RS232 -> mboardd
SOUNDS["CDDA"] = true          -- e6400 SCSI CD-ROM

--------------------------------------------------
-- specify available video cores
--------------------------------------------------

VIDEOS["T6963C"] = true  -- e6400
VIDEOS["HD44780"] = true -- emu3
VIDEOS["MC6845"] = true  -- RS232 -> heath_h19 -> tlb

--------------------------------------------------
-- specify available machine cores
--------------------------------------------------

MACHINES["UPD765"] = true    -- e6400
MACHINES["FDC_PLL"] = true   -- UPD765
MACHINES["EEPROMDEV"] = true  -- e6400 93C46 EEPROM

MACHINES["WD_FDC"] = true    -- emu3
MACHINES["NCR5380"] = true   -- emu3
MACHINES["ACIA6850"] = true  -- emu3
MACHINES["PIT8253"] = true   -- emu3
MACHINES["NSCSI"] = true     -- emu3
MACHINES["Z80SCC"] = true    -- emu3
MACHINES["Z80DAISY"] = true  -- Z80SCC

MACHINES["INS8250"] = true   -- RS232 -> heath_h19 -> tlb
MACHINES["MM5740"] = true    -- RS232 -> heath_h19 -> tlb
MACHINES["PCF8573"] = true   -- RS232 -> scorpion
MACHINES["VOTRAXTNT"] = true  -- RS232 -> votraxtnt bus device
MACHINES["IE15"] = true       -- RS232 -> ie15 bus device
MACHINES["SWTPC8212"] = true  -- RS232 -> swtpc8212 bus device
MACHINES["6821PIA"] = true    -- RS232 -> swtpc8212
MACHINES["INPUT_MERGER"] = true -- RS232 -> swtpc8212
MACHINES["MC68901"] = true      -- e6400 MFP

--------------------------------------------------
-- specify available bus cores
--------------------------------------------------

BUSES["NSCSI"] = true           -- emu3
BUSES["MIDI"] = true            -- e6400
BUSES["RS232"] = true           -- emu3
BUSES["HEATHZENITH_H19"] = true -- RS232 -> heath_h19
BUSES["SUNKBD"] = true          -- RS232 -> sun_kbd

--------------------------------------------------
-- This is the list of files that are necessary
-- for building all of the drivers referenced
-- in tiny.lst
--------------------------------------------------

function createProjects_mame_tiny(_target, _subtarget)
	project ("mame_tiny")
	targetsubdir(_target .."_" .. _subtarget)
	kind (LIBTYPE)
	uuid (os.uuid("drv-mame-tiny"))
	addprojectflags()
	precompiledheaders_novs()

	includedirs {
		MAME_DIR .. "src/osd",
		MAME_DIR .. "src/emu",
		MAME_DIR .. "src/devices",
		MAME_DIR .. "src/mame/shared",
		MAME_DIR .. "src/lib",
		MAME_DIR .. "src/lib/util",
		MAME_DIR .. "3rdparty",
		GEN_DIR  .. "mame/layout",
	}

files{
	-- e6400
	MAME_DIR .. "src/mame/emusys/e6400.cpp",
	-- eiii
	-- MAME_DIR .. "src/mame/drivers/emuiii.cpp",	
	MAME_DIR .. "src/mame/emusys/emu3.cpp",	
}
end

function linkProjects_mame_tiny(_target, _subtarget)
	links {
		"mame_tiny",
	}
end
