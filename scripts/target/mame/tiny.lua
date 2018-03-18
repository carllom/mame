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

SOUNDS["DAC"] = true

--------------------------------------------------
-- specify available video cores
--------------------------------------------------

--------------------------------------------------
-- specify available machine cores
--------------------------------------------------

MACHINES["SA16"] = true
MACHINES["HD44780"] = true
MACHINES["TMS3556"] = true
MACHINES["WD_FDC"] = true

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
	MAME_DIR .. "src/mame/drivers/s330.c",
	MAME_DIR .. "src/mame/includes/carpolo.h",
	MAME_DIR .. "src/mame/machine/carpolo.cpp",
	MAME_DIR .. "src/mame/video/carpolo.cpp",
	MAME_DIR .. "src/mame/drivers/circus.cpp",
	MAME_DIR .. "src/mame/includes/circus.h",
	MAME_DIR .. "src/mame/audio/circus.cpp",
	MAME_DIR .. "src/mame/video/circus.cpp",
	MAME_DIR .. "src/mame/drivers/exidy.cpp",
	MAME_DIR .. "src/mame/includes/exidy.h",
	MAME_DIR .. "src/mame/audio/exidy.cpp",
	MAME_DIR .. "src/mame/audio/exidy.h",
	MAME_DIR .. "src/mame/video/exidy.cpp",
	MAME_DIR .. "src/mame/audio/exidy440.cpp",
	MAME_DIR .. "src/mame/audio/exidy440.h",
	MAME_DIR .. "src/mame/drivers/starfire.cpp",
	MAME_DIR .. "src/mame/includes/starfire.h",
	MAME_DIR .. "src/mame/video/starfire.cpp",
	MAME_DIR .. "src/mame/drivers/vertigo.cpp",
	MAME_DIR .. "src/mame/includes/vertigo.h",
	MAME_DIR .. "src/mame/machine/vertigo.cpp",
	MAME_DIR .. "src/mame/video/vertigo.cpp",
	MAME_DIR .. "src/mame/drivers/victory.cpp",
	MAME_DIR .. "src/mame/includes/victory.h",
	MAME_DIR .. "src/mame/video/victory.cpp",
	MAME_DIR .. "src/mame/audio/targ.cpp",
	MAME_DIR .. "src/mame/drivers/astrocde.cpp",
	MAME_DIR .. "src/mame/includes/astrocde.h",
	MAME_DIR .. "src/mame/video/astrocde.cpp",
	MAME_DIR .. "src/mame/drivers/gridlee.cpp",
	MAME_DIR .. "src/mame/includes/gridlee.h",
	MAME_DIR .. "src/mame/audio/gridlee.cpp",
	MAME_DIR .. "src/mame/video/gridlee.cpp",
	MAME_DIR .. "src/mame/drivers/williams.cpp",
	MAME_DIR .. "src/mame/includes/williams.h",
	MAME_DIR .. "src/mame/machine/williams.cpp",
	MAME_DIR .. "src/mame/audio/williams.cpp",
	MAME_DIR .. "src/mame/audio/williams.h",
	MAME_DIR .. "src/mame/video/williams.cpp",
	MAME_DIR .. "src/mame/drivers/gaelco.cpp",
	MAME_DIR .. "src/mame/includes/gaelco.h",
	MAME_DIR .. "src/mame/video/gaelco.cpp",
	MAME_DIR .. "src/mame/machine/gaelcrpt.cpp",
	MAME_DIR .. "src/mame/drivers/wrally.cpp",
	MAME_DIR .. "src/mame/includes/wrally.h",
	MAME_DIR .. "src/mame/machine/wrally.cpp",
	MAME_DIR .. "src/mame/video/wrally.cpp",
	MAME_DIR .. "src/mame/machine/gaelco_ds5002fp.cpp",
	MAME_DIR .. "src/mame/machine/gaelco_ds5002fp.h",
	MAME_DIR .. "src/mame/drivers/looping.cpp",
	MAME_DIR .. "src/mame/drivers/supertnk.cpp",
}
end

function linkProjects_mame_tiny(_target, _subtarget)
	links {
		"mame_tiny",
	}
end
