package com.testbed.nfcrecipetag.core.tagmodel

import java.io.Serializable

/**
 * Builds a one-step test recipe: Shaker, duration 5 s. For testing the reader firmware.
 * Header: ID=1, RecipeSteps=1; RightNumber and CheckSum are set by encodeRecipe() when writing.
 */
fun createTestRecipeShaker5(unknownTail: ByteArray = ByteArray(0)): DecodedRecipe {
    val header = RecipeHeader(
        id = 1,
        numOfDrinks = 0,
        recipeSteps = 1,
        actualRecipeStep = 0,
        actualBudget = 0,
        parameters = 0,
        rightNumber = 254,
        recipeDone = false,
        checksum = 0
    )
    val step = ProcessTypes.createStep(ProcessTypes.SHAKER, 5, 0, 0)
    return DecodedRecipe(
        header = header,
        steps = listOf(step),
        rawBytes = ByteArray(0),
        unknownTail = unknownTail,
        checksumValid = false,
        integrityValid = true
    )
}

/**
 * Full decoded recipe: header + steps + original raw bytes for preservation.
 * unknownTail: bytes after the last step (preserved when re-encoding).
 */
data class DecodedRecipe(
    val header: RecipeHeader,
    val steps: List<RecipeStep>,
    val rawBytes: ByteArray,
    val unknownTail: ByteArray = ByteArray(0),
    val checksumValid: Boolean = false,
    val integrityValid: Boolean = false
) : Serializable {
    override fun equals(other: Any?): Boolean {
        if (this === other) return true
        if (other !is DecodedRecipe) return false
        return header == other.header &&
            steps == other.steps &&
            rawBytes.contentEquals(other.rawBytes) &&
            unknownTail.contentEquals(other.unknownTail)
    }
    override fun hashCode(): Int {
        var result = header.hashCode()
        result = 31 * result + steps.hashCode()
        result = 31 * result + rawBytes.contentHashCode()
        result = 31 * result + unknownTail.contentHashCode()
        return result
    }
}
