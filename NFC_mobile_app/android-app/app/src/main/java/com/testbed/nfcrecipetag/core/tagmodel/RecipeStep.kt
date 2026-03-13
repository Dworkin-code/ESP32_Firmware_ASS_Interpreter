package com.testbed.nfcrecipetag.core.tagmodel

import java.io.Serializable

/**
 * Decoded TRecipeStep (32 bytes on tag).
 * See tag_format_spec.md section 6.
 */
data class RecipeStep(
    val id: Int,
    val nextId: Int,
    val typeOfProcess: Int,
    val parameterProcess1: Int,
    val parameterProcess2: Int,
    val priceForTransport: Int,
    val transportCellId: Int,
    val transportCellReservationId: Int,
    val priceForProcess: Int,
    val processCellId: Int,
    val processCellReservationId: Int,
    val timeOfProcess: Long,
    val timeOfTransport: Long,
    val needForTransport: Boolean,
    val isTransport: Boolean,
    val isProcess: Boolean,
    val isStepDone: Boolean
) : Serializable
