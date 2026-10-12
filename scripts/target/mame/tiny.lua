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

CPUS["NC4000"] = true

--------------------------------------------------
-- The remaining cores are needed by the RS-232
-- port slot devices (generated with
-- scripts/build/makedep.py sourcesproject).
--------------------------------------------------

CPUS["ADSP21XX"] = true
CPUS["IE15"] = true
CPUS["M6800"] = true
CPUS["MCS48"] = true
CPUS["MCS51"] = true
CPUS["Z80"] = true

SOUNDS["AY8910"] = true
SOUNDS["BEEP"] = true
SOUNDS["VOTRAX_SC01"] = true
SOUNDS["VOTRAX_SC01A"] = true

VIDEOS["MC6845"] = true
VIDEOS["SCN2674"] = true

MACHINES["6821PIA"] = true
MACHINES["ACIA6850"] = true
MACHINES["IE15"] = true
MACHINES["INPUT_MERGER"] = true
MACHINES["INS8250"] = true
MACHINES["MM5740"] = true
MACHINES["PCF8573"] = true
MACHINES["S97801"] = true
MACHINES["SCN_PCI"] = true
MACHINES["SWTPC8212"] = true
MACHINES["VOTRAXTNT"] = true
MACHINES["Z80DAISY"] = true

BUSES["HEATHZENITH_H19"] = true
BUSES["RS232"] = true
BUSES["SUNKBD"] = true


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
	MAME_DIR .. "src/mame/misc/nb4200.cpp",
}
end

function linkProjects_mame_tiny(_target, _subtarget)
	links {
		"mame_tiny",
	}
end
