package com.testbed.nfcrecipetag.core.tagmodel

import java.io.Serializable

/**
 * Decoded TRecipeInfo header (12 bytes on tag).
 * See tag_format_spec.md section 4.
 */
data class RecipeHeader(
    val id: Int,
    val numOfDrinks: Int,
    val recipeSteps: Int,
    val actualRecipeStep: Int,
    val actualBudget: Int,
    val parameters: Int,
    val rightNumber: Int,
    val recipeDone: Boolean,
    val checksum: Int
) : Serializable {
    fun isValidIntegrity(): Boolean = (id + rightNumber) == 255
}
