package com.testbed.nfcrecipetag.core.cellprofile

/**
 * Registry of predefined Testbed cell profiles. Add new profiles here to support
 * additional cells without refactoring the rest of the app.
 */
object CellProfileRegistry {

    /**
     * Current active cell used with the PLC AAS implementation.
     * GetSupported expects: material in 1..4, supportA in 21..60, supportB in 21..60.
     * Test combination for GetSupported/ReserveAction: material=3, supportA=21, supportB=21.
     */
    val CURRENT_TESTBED_CELL = CellProfile(
        id = "current_testbed_cell",
        displayName = "Current Testbed Cell (PLC AAS)",
        materialRange = 1..4,
        supportARange = 21..60,
        supportBRange = 21..60,
        defaultMaterial = 3,
        defaultSupportA = 21,
        defaultSupportB = 21,
        notes = "Active cell; PLC GetSupported validates material 1..4, supportA/supportB 21..60."
    )

    /** All predefined profiles. Add new cells here. */
    val ALL_PROFILES: List<CellProfile> = listOf(
        CURRENT_TESTBED_CELL
    )

    /** Default profile ID when none is stored (e.g. first install). */
    const val DEFAULT_PROFILE_ID: String = "current_testbed_cell"

    fun getById(id: String): CellProfile? = ALL_PROFILES.find { it.id == id }

    /** Returns the profile for the given id, or the default profile. */
    fun getByIdOrDefault(id: String?): CellProfile =
        if (id != null) getById(id) ?: getById(DEFAULT_PROFILE_ID)!!
        else getById(DEFAULT_PROFILE_ID)!!
}
