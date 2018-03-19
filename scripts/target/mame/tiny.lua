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
CPUS["NS32016"] = true -- eiii

--------------------------------------------------
-- Specify all the sound cores necessary for the
-- drivers referenced in tiny.lst.
--------------------------------------------------

SOUNDS["DAC"] = true

--------------------------------------------------
-- specify available video cores
--------------------------------------------------

VIDEOS["T6963"] = true -- e6400

--------------------------------------------------
-- specify available machine cores
--------------------------------------------------

MACHINES["WD_FDC"] = true -- emuiii
MACHINES["UPD765"] = true -- e6400
MACHINES["NCR5380"] = true -- emuiii

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
	-- e6400
	MAME_DIR .. "src/mame/drivers/e6400.cpp",
	MAME_DIR .. "src/mame/video/t6963.cpp",
	MAME_DIR .. "src/mame/video/t6963.h",

	-- eiii
	MAME_DIR .. "src/mame/drivers/emuiii.cpp",	
}
end

function linkProjects_mame_tiny(_target, _subtarget)
	links {
		"mame_tiny",
	}
end
