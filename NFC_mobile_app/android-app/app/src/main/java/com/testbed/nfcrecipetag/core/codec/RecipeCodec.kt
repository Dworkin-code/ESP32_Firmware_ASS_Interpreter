package com.testbed.nfcrecipetag.core.codec

import android.util.Log
import com.testbed.nfcrecipetag.core.tagmodel.DecodedRecipe
import com.testbed.nfcrecipetag.core.tagmodel.ProcessTypes
import com.testbed.nfcrecipetag.core.tagmodel.RecipeHeader
import com.testbed.nfcrecipetag.core.tagmodel.RecipeStep
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * Struct sizes from tag_format_spec.md (must match ESP32 firmware).
 * Assumed; update if firmware sizeof() differs.
 */
const val HEADER_SIZE = 12
const val STEP_SIZE = 32

/** TRecipeStep byte offsets (firmware NFC_reader.h). */
const val STEP_OFFSET_ID = 0
const val STEP_OFFSET_NEXT_ID = 1
const val STEP_OFFSET_TYPE_OF_PROCESS = 2
const val STEP_OFFSET_PARAMETER_PROCESS1 = 3

/**
 * Checksum over TRecipeStep[] only: sum of (byte[i] * ((i % 4) + 1)).
 * See tag_format_spec.md section 5.
 */
fun computeStepChecksum(stepBytes: ByteArray): Int {
    var sum = 0
    for (i in stepBytes.indices) {
        sum += (stepBytes[i].toInt() and 0xFF) * ((i % 4) + 1)
    }
    return sum and 0xFFFF
}

/**
 * Decode bytes [0..HEADER_SIZE-1] as TRecipeInfo (little-endian).
 */
fun decodeHeader(bytes: ByteArray): RecipeHeader {
    require(bytes.size >= HEADER_SIZE)
    val buf = ByteBuffer.wrap(bytes, 0, HEADER_SIZE).order(ByteOrder.LITTLE_ENDIAN)
    return RecipeHeader(
        id = buf.get(0).toInt() and 0xFF,
        numOfDrinks = buf.getShort(1).toInt() and 0xFFFF,
        recipeSteps = buf.get(3).toInt() and 0xFF,
        actualRecipeStep = buf.get(4).toInt() and 0xFF,
        actualBudget = buf.getShort(5).toInt() and 0xFFFF,
        parameters = buf.get(7).toInt() and 0xFF,
        rightNumber = buf.get(8).toInt() and 0xFF,
        recipeDone = buf.get(9).toInt() != 0,
        checksum = buf.getShort(10).toInt() and 0xFFFF
    )
}

/**
 * Decode one TRecipeStep at offset (32 bytes). Bitfield order assumed LSB first.
 * TypeOfProcess is at step byte offset 2 (firmware TRecipeStep.TypeOfProcess).
 */
fun decodeStep(bytes: ByteArray, offset: Int): RecipeStep {
    require(bytes.size >= offset + STEP_SIZE)
    // Read TypeOfProcess and ParameterProcess1 directly from byte array (step offset 2 and 3) to avoid any ByteBuffer offset confusion
    val typeOfProcess = bytes[offset + STEP_OFFSET_TYPE_OF_PROCESS].toInt() and 0xFF
    val parameterProcess1 = bytes[offset + STEP_OFFSET_PARAMETER_PROCESS1].toInt() and 0xFF
    if (Log.isLoggable(TAG_RECIPE_CODEC, Log.DEBUG)) {
        val stepHex = bytes.copyOfRange(offset, offset + minOf(8, STEP_SIZE))
            .joinToString(" ") { "%02X".format(it) }
        Log.d(TAG_RECIPE_CODEC, "Raw step bytes (first 8): $stepHex ... | Decoded TypeOfProcess=$typeOfProcess ParameterProcess1=$parameterProcess1 | Display=${ProcessTypes.name(typeOfProcess)}")
    }
    val buf = ByteBuffer.wrap(bytes, offset, STEP_SIZE).order(ByteOrder.LITTLE_ENDIAN)
    val flags = buf.get(31).toInt() and 0xFF
    return RecipeStep(
        id = bytes[offset + STEP_OFFSET_ID].toInt() and 0xFF,
        nextId = bytes[offset + STEP_OFFSET_NEXT_ID].toInt() and 0xFF,
        typeOfProcess = typeOfProcess,
        parameterProcess1 = parameterProcess1,
        parameterProcess2 = buf.getShort(4).toInt() and 0xFFFF,
        priceForTransport = buf.get(6).toInt() and 0xFF,
        transportCellId = buf.get(7).toInt() and 0xFF,
        transportCellReservationId = buf.getShort(8).toInt() and 0xFFFF,
        priceForProcess = buf.get(10).toInt() and 0xFF,
        processCellId = buf.get(11).toInt() and 0xFF,
        processCellReservationId = buf.getShort(12).toInt() and 0xFFFF,
        timeOfProcess = buf.getLong(14),
        timeOfTransport = buf.getLong(22),
        needForTransport = (flags and 1) != 0,
        isTransport = (flags and 2) != 0,
        isProcess = (flags and 4) != 0,
        isStepDone = (flags and 8) != 0
    )
}

/**
 * Decode full recipe stream. Preserves unknown tail bytes.
 */
fun decodeRecipe(rawBytes: ByteArray): DecodedRecipe? {
    if (rawBytes.size < HEADER_SIZE) return null
    val header = decodeHeader(rawBytes)
    val stepCount = header.recipeSteps.coerceAtLeast(0)
    val stepsEnd = HEADER_SIZE + stepCount * STEP_SIZE
    if (rawBytes.size < stepsEnd) return null

    val steps = (0 until stepCount).map { decodeStep(rawBytes, HEADER_SIZE + it * STEP_SIZE) }
    val stepBytes = rawBytes.copyOfRange(HEADER_SIZE, stepsEnd)
    val computedChecksum = if (stepCount == 0) 0 else computeStepChecksum(stepBytes)
    val checksumValid = (computedChecksum == header.checksum)
    val integrityValid = header.isValidIntegrity()

    val unknownTail = if (rawBytes.size > stepsEnd) rawBytes.copyOfRange(stepsEnd, rawBytes.size) else ByteArray(0)
    return DecodedRecipe(
        header = header,
        steps = steps,
        rawBytes = rawBytes,
        unknownTail = unknownTail,
        checksumValid = checksumValid,
        integrityValid = integrityValid
    )
}

/**
 * Encode header to 12 bytes (little-endian). Caller must set RightNumber = 255 - ID and CheckSum after steps.
 */
fun encodeHeader(header: RecipeHeader): ByteArray {
    val buf = ByteBuffer.allocate(HEADER_SIZE).order(ByteOrder.LITTLE_ENDIAN)
    buf.put(header.id.toByte())
    buf.putShort(header.numOfDrinks.toShort())
    buf.put(header.recipeSteps.toByte())
    buf.put(header.actualRecipeStep.toByte())
    buf.putShort(header.actualBudget.toShort())
    buf.put(header.parameters.toByte())
    buf.put(header.rightNumber.toByte())
    buf.put(if (header.recipeDone) 1 else 0)
    buf.putShort(header.checksum.toShort())
    return buf.array()
}

private const val TAG_RECIPE_CODEC = "RecipeCodec"

/**
 * Encode one step to 32 bytes. TypeOfProcess at byte 2, ParameterProcess1 at byte 3 (firmware layout).
 * Flags: bit0=NeedForTransport, bit1=IsTransport, bit2=IsProcess, bit3=IsStepDone.
 */
fun encodeStep(step: RecipeStep): ByteArray {
    val buf = ByteBuffer.allocate(STEP_SIZE).order(ByteOrder.LITTLE_ENDIAN)
    buf.put(STEP_OFFSET_ID, step.id.toByte())
    buf.put(STEP_OFFSET_NEXT_ID, step.nextId.toByte())
    buf.put(STEP_OFFSET_TYPE_OF_PROCESS, step.typeOfProcess.toByte())
    buf.put(STEP_OFFSET_PARAMETER_PROCESS1, step.parameterProcess1.toByte())
    buf.position(4)
    buf.putShort(step.parameterProcess2.toShort())
    buf.put(step.priceForTransport.toByte())
    buf.put(step.transportCellId.toByte())
    buf.putShort(step.transportCellReservationId.toShort())
    buf.put(step.priceForProcess.toByte())
    buf.put(step.processCellId.toByte())
    buf.putShort(step.processCellReservationId.toShort())
    buf.putLong(step.timeOfProcess)
    buf.putLong(step.timeOfTransport)
    buf.put(30, 0) // padding
    val flags = (if (step.needForTransport) 1 else 0) or
        (if (step.isTransport) 2 else 0) or
        (if (step.isProcess) 4 else 0) or
        (if (step.isStepDone) 8 else 0)
    buf.put(31, flags.toByte())
    val array = buf.array()
    // Sanity check: byte at offset 2 must equal step.typeOfProcess (e.g. 3 for Shaker)
    if (array[STEP_OFFSET_TYPE_OF_PROCESS].toInt() and 0xFF != step.typeOfProcess) {
        Log.w(TAG_RECIPE_CODEC, "encodeStep: byte[2]=${array[2].toInt() and 0xFF} but step.typeOfProcess=${step.typeOfProcess}")
    }
    return array
}

/**
 * Encode full recipe: header (with recomputed RightNumber and CheckSum) + steps + unknownTail.
 */
fun encodeRecipe(header: RecipeHeader, steps: List<RecipeStep>, unknownTail: ByteArray): ByteArray {
    val rightNumber = 255 - header.id
    val stepBytes = steps.flatMap { encodeStep(it).toList() }.toByteArray()
    val checksum = if (steps.isEmpty()) 0 else computeStepChecksum(stepBytes)
    val h = header.copy(rightNumber = rightNumber, checksum = checksum)
    val headerBytes = encodeHeader(h)
    val stream = headerBytes + stepBytes + unknownTail
    // First step TypeOfProcess is at stream index HEADER_SIZE + 2 = 14; must match steps[0].typeOfProcess
    if (steps.isNotEmpty() && stream.size > HEADER_SIZE + STEP_OFFSET_TYPE_OF_PROCESS) {
        val firstStepTypeByte = stream[HEADER_SIZE + STEP_OFFSET_TYPE_OF_PROCESS].toInt() and 0xFF
        if (firstStepTypeByte != steps[0].typeOfProcess && Log.isLoggable(TAG_RECIPE_CODEC, Log.WARN)) {
            Log.w(TAG_RECIPE_CODEC, "encodeRecipe: stream[14]=$firstStepTypeByte but steps[0].typeOfProcess=${steps[0].typeOfProcess}")
        }
    }
    return stream
}
