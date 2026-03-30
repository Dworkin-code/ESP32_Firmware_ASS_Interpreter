package com.testbed.nfcrecipetag.core.codec

import android.util.Log
import com.testbed.nfcrecipetag.core.tagmodel.DecodedRecipe
import com.testbed.nfcrecipetag.core.tagmodel.ProcessTypes
import com.testbed.nfcrecipetag.core.tagmodel.RecipeHeader
import com.testbed.nfcrecipetag.core.tagmodel.RecipeStep
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * Struct sizes (must match ESP32 firmware packed layout).
 * HEADER_SIZE = 12 (TRecipeInfo). STEP_SIZE = 31 (TRecipeStep packed, no trailing padding).
 * Layout: 0-1 id,nextId; 2 typeOfProcess; 3 parameterProcess1; 4-5 parameterProcess2 (uint16 LE);
 * 6-13 transport/process fields; 14-21 TimeOfProcess; 22-29 TimeOfTransport; 30 flags.
 */
const val HEADER_SIZE = 12
/** TRecipeStep packed size: 31 bytes. Flags at byte 30. Confirmed from real tag (step 1 starts at header+31). */
const val STEP_SIZE = 31

/** NTAG213 user memory (bytes). Recipe payload must not exceed this. */
const val NTAG213_USER_MEMORY = 144

/** Maximum recipe steps that fit in NTAG213: floor((NTAG213_USER_MEMORY - HEADER_SIZE) / STEP_SIZE) = 4. */
val NTAG213_MAX_STEPS: Int get() = (NTAG213_USER_MEMORY - HEADER_SIZE) / STEP_SIZE

/** Byte offset of the flags byte (NeedForTransport, IsTransport, IsProcess, IsStepDone) within one step. */
const val STEP_OFFSET_FLAGS = 30

/** TRecipeStep byte offsets (firmware NFC_reader.h). */
const val STEP_OFFSET_ID = 0
const val STEP_OFFSET_NEXT_ID = 1
const val STEP_OFFSET_TYPE_OF_PROCESS = 2
const val STEP_OFFSET_PARAMETER_PROCESS1 = 3
/** parameterProcess2 is uint16 little-endian at offsets 4-5. */
const val STEP_OFFSET_PARAMETER_PROCESS2_LO = 4
const val STEP_OFFSET_PARAMETER_PROCESS2_HI = 5

/** Read uint16 little-endian from bytes at given offset (bytes[offset] = low byte, bytes[offset+1] = high byte). */
private fun readUInt16LE(bytes: ByteArray, offset: Int): Int {
    require(offset + 1 < bytes.size)
    return (bytes[offset].toInt() and 0xFF) or ((bytes[offset + 1].toInt() and 0xFF) shl 8)
}

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
 * Decode one TRecipeStep at offset (31 bytes packed). parameterProcess2 is uint16 LE at bytes 4-5. Flags at byte 30.
 */
fun decodeStep(bytes: ByteArray, offset: Int): RecipeStep {
    require(bytes.size >= offset + STEP_SIZE) { "need ${offset + STEP_SIZE} bytes, have ${bytes.size}" }
    val typeOfProcess = bytes[offset + STEP_OFFSET_TYPE_OF_PROCESS].toInt() and 0xFF
    val parameterProcess1 = bytes[offset + STEP_OFFSET_PARAMETER_PROCESS1].toInt() and 0xFF
    val parameterProcess2 = readUInt16LE(bytes, offset + STEP_OFFSET_PARAMETER_PROCESS2_LO)
    if (Log.isLoggable(TAG_RECIPE_CODEC, Log.DEBUG)) {
        val stepHex = bytes.copyOfRange(offset, offset + minOf(8, STEP_SIZE))
            .joinToString(" ") { "%02X".format(it) }
        Log.d(TAG_RECIPE_CODEC, "Raw step bytes (first 8): $stepHex ... | Decoded TypeOfProcess=$typeOfProcess param1=$parameterProcess1 param2=$parameterProcess2 | Display=${ProcessTypes.name(typeOfProcess)}")
    }
    val buf = ByteBuffer.wrap(bytes, offset, STEP_SIZE).order(ByteOrder.LITTLE_ENDIAN)
    val flags = bytes[offset + STEP_OFFSET_FLAGS].toInt() and 0xFF
    return RecipeStep(
        id = bytes[offset + STEP_OFFSET_ID].toInt() and 0xFF,
        nextId = bytes[offset + STEP_OFFSET_NEXT_ID].toInt() and 0xFF,
        typeOfProcess = typeOfProcess,
        parameterProcess1 = parameterProcess1,
        parameterProcess2 = parameterProcess2,
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
 *
 * For legacy / partial tags (e.g. Ultralight read limited to 128 bytes) the header
 * RecipeSteps may be larger than the number of full steps we actually have bytes for.
 * In that case we decode as many *complete* steps as fit into rawBytes and mark
 * checksum as invalid instead of dropping the whole decode.
 */
fun decodeRecipe(rawBytes: ByteArray): DecodedRecipe? {
    if (rawBytes.size < HEADER_SIZE) return null
    val header = decodeHeader(rawBytes)

    // How many full STEP_SIZE steps are actually present in the buffer?
    val availableForSteps = (rawBytes.size - HEADER_SIZE).coerceAtLeast(0)
    val maxStepsByLength = availableForSteps / STEP_SIZE
    val headerStepCount = header.recipeSteps.coerceAtLeast(0)

    // Decode up to the smaller of (header.RecipeSteps, bytes-based capacity).
    val stepCount = minOf(headerStepCount, maxStepsByLength)
    val stepsEnd = HEADER_SIZE + stepCount * STEP_SIZE
    // Step bytes for checksum: only up to stepsEnd, never beyond NTAG213 user memory.
    val checksumEnd = minOf(stepsEnd, rawBytes.size, NTAG213_USER_MEMORY)

    val steps = (0 until stepCount).map { decodeStep(rawBytes, HEADER_SIZE + it * STEP_SIZE) }
    val stepBytes = if (stepCount > 0 && checksumEnd > HEADER_SIZE) {
        rawBytes.copyOfRange(HEADER_SIZE, checksumEnd)
    } else {
        ByteArray(0)
    }

    // Only trust checksum if we had enough bytes to cover all steps declared in the header
    // and header did not exceed NTAG213 capacity.
    val checksumValid = if (stepCount == headerStepCount && stepCount > 0 && headerStepCount <= NTAG213_MAX_STEPS) {
        val computed = computeStepChecksum(stepBytes)
        computed == header.checksum
    } else {
        false
    }
    val integrityValid = header.isValidIntegrity()

    val unknownTail = if (rawBytes.size > stepsEnd) {
        rawBytes.copyOfRange(stepsEnd, rawBytes.size)
    } else {
        ByteArray(0)
    }

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
 * Encode one step to 31 bytes (packed). parameterProcess2 at bytes 4-5 (uint16 LE). Flags at byte 30.
 */
fun encodeStep(step: RecipeStep): ByteArray {
    val buf = ByteBuffer.allocate(STEP_SIZE).order(ByteOrder.LITTLE_ENDIAN)
    buf.put(STEP_OFFSET_ID, step.id.toByte())
    buf.put(STEP_OFFSET_NEXT_ID, step.nextId.toByte())
    buf.put(STEP_OFFSET_TYPE_OF_PROCESS, step.typeOfProcess.toByte())
    buf.put(STEP_OFFSET_PARAMETER_PROCESS1, step.parameterProcess1.toByte())
    buf.putShort(STEP_OFFSET_PARAMETER_PROCESS2_LO, (step.parameterProcess2 and 0xFFFF).toShort())
    buf.position(6)
    buf.put(step.priceForTransport.toByte())
    buf.put(step.transportCellId.toByte())
    buf.putShort(step.transportCellReservationId.toShort())
    buf.put(step.priceForProcess.toByte())
    buf.put(step.processCellId.toByte())
    buf.putShort(step.processCellReservationId.toShort())
    buf.putLong(step.timeOfProcess)
    buf.putLong(step.timeOfTransport)
    val flags = (if (step.needForTransport) 1 else 0) or
        (if (step.isTransport) 2 else 0) or
        (if (step.isProcess) 4 else 0) or
        (if (step.isStepDone) 8 else 0)
    buf.put(STEP_OFFSET_FLAGS, flags.toByte())
    val array = buf.array()
    // Sanity check: byte at offset 2 must equal step.typeOfProcess (e.g. 3 for Shaker)
    if (array[STEP_OFFSET_TYPE_OF_PROCESS].toInt() and 0xFF != step.typeOfProcess) {
        Log.w(TAG_RECIPE_CODEC, "encodeStep: byte[2]=${array[2].toInt() and 0xFF} but step.typeOfProcess=${step.typeOfProcess}")
    }
    return array
}

/**
 * Encode full recipe: header (with recomputed RightNumber and CheckSum) + steps + unknownTail.
 * Rejects if recipe would exceed NTAG213 user memory (144 bytes).
 */
fun encodeRecipe(header: RecipeHeader, steps: List<RecipeStep>, unknownTail: ByteArray): ByteArray {
    if (steps.size > NTAG213_MAX_STEPS) {
        throw IllegalArgumentException(
            "Recipe has ${steps.size} steps but NTAG213 supports at most $NTAG213_MAX_STEPS steps (${NTAG213_USER_MEMORY} bytes user memory)."
        )
    }
    val totalSize = HEADER_SIZE + steps.size * STEP_SIZE + unknownTail.size
    if (totalSize > NTAG213_USER_MEMORY) {
        throw IllegalArgumentException(
            "Encoded recipe size $totalSize bytes exceeds NTAG213 user memory ($NTAG213_USER_MEMORY bytes)."
        )
    }
    val rightNumber = 255 - header.id
    val stepBytes = steps.flatMap { encodeStep(it).toList() }.toByteArray()
    val checksum = if (steps.isEmpty()) 0 else computeStepChecksum(stepBytes)
    val h = header.copy(rightNumber = rightNumber, checksum = checksum)
    if (Log.isLoggable(TAG_RECIPE_CODEC, Log.DEBUG)) {
        steps.forEachIndexed { index, step ->
            Log.d(
                TAG_RECIPE_CODEC,
                "encodeRecipe step#$index: " +
                    "material(TypeOfProcess)=${step.typeOfProcess}, " +
                    "supportA(ParameterProcess1)=${step.parameterProcess1}, " +
                    "supportB(ParameterProcess2)=${step.parameterProcess2}"
            )
        }
        Log.d(
            TAG_RECIPE_CODEC,
            "encodeRecipe header: id=${header.id}, recipeSteps=${steps.size}, " +
                "rightNumber=$rightNumber, checksum=$checksum"
        )
    }
    val headerBytes = encodeHeader(h)
    val stream = headerBytes + stepBytes + unknownTail
    // First step TypeOfProcess is at stream index HEADER_SIZE + 2
    if (steps.isNotEmpty() && stream.size > HEADER_SIZE + STEP_OFFSET_TYPE_OF_PROCESS) {
        val firstStepTypeByte = stream[HEADER_SIZE + STEP_OFFSET_TYPE_OF_PROCESS].toInt() and 0xFF
        if (firstStepTypeByte != steps[0].typeOfProcess && Log.isLoggable(TAG_RECIPE_CODEC, Log.WARN)) {
            Log.w(TAG_RECIPE_CODEC, "encodeRecipe: stream[14]=$firstStepTypeByte but steps[0].typeOfProcess=${steps[0].typeOfProcess}")
        }
    }
    return stream
}
