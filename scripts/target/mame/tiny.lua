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

CPUS["M6800"] = true
CPUS["M6809"] = true

--------------------------------------------------
-- Specify all the sound cores necessary for the
-- drivers referenced in tiny.lst.
--------------------------------------------------

SOUNDS["DAC"] = true

--------------------------------------------------
-- specify available video cores
--------------------------------------------------

VIDEOS["DL1416"] = true

--------------------------------------------------
-- specify available machine cores
--------------------------------------------------

MACHINES["WD_FDC"] = true
MACHINES["ACIA6850"] = true
MACHINES["MOS6551"] = true
MACHINES["I8214"] = true
MACHINES["6821PIA"] = true
MACHINES["6840PTM"] = true
MACHINES["MSM5832"] = true

--------------------------------------------------
-- specify available bus cores
--------------------------------------------------

--BUSES["CENTRONICS"] = true

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
	precompiledheaders()

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
	MAME_DIR .. "src/mame/drivers/cmi_cl.cpp",
	MAME_DIR .. "src/mame/includes/cmi.h",
	MAME_DIR .. "src/mame/machine/cmikbd.cpp",
	MAME_DIR .. "src/mame/machine/cmikbd.h",
}
end

function linkProjects_mame_tiny(_target, _subtarget)
	links {
		"mame_tiny",
	}
end
