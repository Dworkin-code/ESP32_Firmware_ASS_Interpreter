package com.testbed.nfcrecipetag.core.tagmodel

import java.io.Serializable

/**
 * Unified, firmware-compatible recipe model used across all tag types.
 *
 * This intentionally reuses the existing RecipeHeader / RecipeStep / DecodedRecipe
 * types, so the rest of the app continues to work with a single canonical model.
 */
typealias UnifiedRecipeHeader = RecipeHeader
typealias UnifiedRecipeStep = RecipeStep

/**
 * Unified recipe wrapper, independent of physical tag technology.
 *
 * - header / steps: canonical firmware layout (TRecipeInfo / TRecipeStep[])
 * - rawBytes: contiguous recipe byte stream as read from the tag
 * - unknownTail: preserved bytes after the last step (if any)
 * - checksumValid / integrityValid: validation flags from decode
 */
data class UnifiedRecipe(
    val header: UnifiedRecipeHeader,
    val steps: List<UnifiedRecipeStep>,
    val rawBytes: ByteArray,
    val unknownTail: ByteArray = ByteArray(0),
    val checksumValid: Boolean = false,
    val integrityValid: Boolean = false
) : Serializable {
    constructor(decoded: DecodedRecipe) : this(
        header = decoded.header,
        steps = decoded.steps,
        rawBytes = decoded.rawBytes,
        unknownTail = decoded.unknownTail,
        checksumValid = decoded.checksumValid,
        integrityValid = decoded.integrityValid
    )
}

