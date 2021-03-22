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

CPUS["M6800"] = true -- SWTPC8212 (terminal)
CPUS["IE15"] = true -- IE15 (terminal)

--------------------------------------------------
-- Specify all the sound cores necessary for the
-- drivers referenced in tiny.lst.
--------------------------------------------------

SOUNDS["DAC"] = true
SOUNDS["BEEP"] = true -- ???

--------------------------------------------------
-- specify available video cores
--------------------------------------------------

VIDEOS["T6963C"] = true -- e6400
VIDEOS["HD44780"] = true -- emu3
VIDEOS["MC6845"] = true -- SWTPC8212

--------------------------------------------------
-- specify available machine cores
--------------------------------------------------
MACHINES["UPD765"] = true -- e6400

MACHINES["WD_FDC"] = true -- emu3
MACHINES["NCR5380N"] = true -- emu3
MACHINES["ACIA6850"] = true -- emu3
MACHINES["PIT8253"] = true -- emu3
MACHINES["NSCSI"] = true -- emu3
MACHINES["Z80SCC"] = true -- emu3
MACHINES["SWTPC8212"] = true -- ??? terminal machine
MACHINES["6821PIA"] = true -- SWTPC8212
MACHINES["INS8250"] = true -- SWTPC8212
MACHINES["INPUT_MERGER"] = true -- SWTPC8212
MACHINES["IE15"] = true -- ??? terminal machine
MACHINES["Z80DAISY"] = true -- Z80SCC
MACHINES["FDC_PLL"] = true -- UPD765

--------------------------------------------------
-- specify available bus cores
--------------------------------------------------

BUSES["NSCSI"] = true -- emu3
BUSES["SCSI"] = true -- e6400?
BUSES["RS232"] = true -- emu3
BUSES["SUNKBD"] = true -- ???

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
		MAME_DIR .. "src/mame",
		MAME_DIR .. "src/lib",
		MAME_DIR .. "src/lib/util",
		MAME_DIR .. "3rdparty",
		GEN_DIR  .. "mame/layout",
	}

files{
	-- e6400
	MAME_DIR .. "src/mame/drivers/e6400.cpp",
	-- eiii
	-- MAME_DIR .. "src/mame/drivers/emuiii.cpp",	
	MAME_DIR .. "src/mame/drivers/emu3.cpp",	
}
end

function linkProjects_mame_tiny(_target, _subtarget)
	links {
		"mame_tiny",
	}
end
