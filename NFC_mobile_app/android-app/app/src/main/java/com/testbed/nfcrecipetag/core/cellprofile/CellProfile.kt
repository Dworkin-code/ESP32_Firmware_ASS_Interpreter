package com.testbed.nfcrecipetag.core.cellprofile

/**
 * Configuration profile for a Testbed 4.0 cell. Defines valid AAS parameter ranges
 * and defaults used when creating/editing recipes and when validating before write.
 *
 * Semantic mapping to NFC tag (TRecipeStep):
 * - [material] → TypeOfProcess → step byte offset 2 (uint8)
 * - [supportA] → ParameterProcess1 → step byte offset 3 (uint8)
 * - [supportB] → ParameterProcess2 → step bytes 4–5 (uint16 LE)
 *
 * The ESP32 reader sends these to the PLC GetSupported/ReserveAction methods.
 */
data class CellProfile(
    /** Unique key for persistence (e.g. in Settings). */
    val id: String,
    /** Display name shown in Settings and debug logs. */
    val displayName: String,
    /** Valid range for material (encoded as TypeOfProcess). */
    val materialRange: IntRange,
    /** Valid range for supportA (encoded as ParameterProcess1). */
    val supportARange: IntRange,
    /** Valid range for supportB (encoded as ParameterProcess2). */
    val supportBRange: IntRange,
    /** Default material when creating a new step or test recipe. */
    val defaultMaterial: Int,
    /** Default supportA when creating a new step or test recipe. */
    val defaultSupportA: Int,
    /** Default supportB when creating a new step or test recipe. */
    val defaultSupportB: Int,
    /** Optional notes or future metadata (e.g. supported process types). */
    val notes: String? = null
) {
    init {
        require(defaultMaterial in materialRange) {
            "defaultMaterial $defaultMaterial must be in $materialRange"
        }
        require(defaultSupportA in supportARange) {
            "defaultSupportA $defaultSupportA must be in $supportARange"
        }
        require(defaultSupportB in supportBRange) {
            "defaultSupportB $defaultSupportB must be in $supportBRange"
        }
    }

    fun isMaterialValid(value: Int): Boolean = value in materialRange
    fun isSupportAValid(value: Int): Boolean = value in supportARange
    fun isSupportBValid(value: Int): Boolean = value in supportBRange

    /** Validates the step values that map to PLC material, supportA, supportB. */
    fun validateStepValues(material: Int, supportA: Int, supportB: Int): Boolean =
        isMaterialValid(material) && isSupportAValid(supportA) && isSupportBValid(supportB)
}
