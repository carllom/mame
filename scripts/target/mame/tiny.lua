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

CPUS["MCS96"] = true

--------------------------------------------------
-- Specify all the sound cores necessary for the
-- drivers referenced in tiny.lst.
--------------------------------------------------

SOUNDS["SA16"] = true
SOUNDS["DAC"] = true

--------------------------------------------------
-- specify available video cores
--------------------------------------------------

VIDEOS["HD44780"] = true -- s330
VIDEOS["TMS3556"] = true -- s330
VIDEOS["T6963C"] = true -- ajr

--------------------------------------------------
-- specify available machine cores
--------------------------------------------------

MACHINES["WD_FDC"] = true
MACHINES["FDC_PLL"] = true -- wd_fdc
MACHINES["BANKDEV"] = true -- ajr

--------------------------------------------------
-- specify available bus cores
--------------------------------------------------

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
	-- MAME_DIR .. "src/mame/drivers/s330.cpp",
	MAME_DIR .. "src/mame/roland/roland_s50.cpp",
	MAME_DIR .. "src/mame/roland/mb63h149.cpp", -- (ajr) not mapped to machine core yet?
	-- MAME_DIR .. "src/mame/roland/mb63h149.h",
	MAME_DIR .. "src/mame/roland/sa16.cpp", -- not mapped to audio core yet
	-- MAME_DIR .. "src/mame/roland/sa16.h",
	MAME_DIR .. "src/mame/roland/bu3905.cpp", -- (ajr) not mapped to audio core yet?
	-- MAME_DIR .. "src/mame/roland/bu3905.h",
}
end

function linkProjects_mame_tiny(_target, _subtarget)
	links {
		"mame_tiny",
	}
end
