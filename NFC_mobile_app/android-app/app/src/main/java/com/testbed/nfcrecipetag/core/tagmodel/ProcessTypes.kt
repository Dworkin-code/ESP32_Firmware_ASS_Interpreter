package com.testbed.nfcrecipetag.core.tagmodel

/**
 * Process type codes stored on the NFC tag (TRecipeStep.TypeOfProcess).
 * Must match ESP32 NFC_recipes.h enum: 0=ToStorageGlass, 1=StorageAlcohol, 2=StorageNonAlcohol, 3=Shaker, ...
 * Tag stores this at step byte offset 2.
 */
object ProcessTypes {
    const val TO_STORAGE_GLASS = 0
    const val STORAGE_ALCOHOL = 1
    const val STORAGE_NON_ALCOHOL = 2
    /** Shaker: ParameterProcess1 = duration in seconds. */
    const val SHAKER = 3
    const val CLEANER = 4
    const val SODA_MAKE = 5
    const val TO_CUSTOMER = 6
    const val TRANSPORT = 7
    const val BUFFER = 8

    /** Display names for UI. Must match firmware enum order. */
    val ALL = listOf(
        TO_STORAGE_GLASS to "ToStorageGlass",
        STORAGE_ALCOHOL to "StorageAlcohol",
        STORAGE_NON_ALCOHOL to "StorageNonAlcohol",
        SHAKER to "Shaker",
        CLEANER to "Cleaner",
        SODA_MAKE to "SodaMake",
        TO_CUSTOMER to "ToCustomer",
        TRANSPORT to "Transport",
        BUFFER to "Buffer"
    )

    fun name(value: Int): String = ALL.find { it.first == value }?.second ?: "Type$value"

    /** Creates a step with given process type and param1; other fields zero. For Shaker, param1 = duration in seconds. */
    fun createStep(typeOfProcess: Int, parameterProcess1: Int, id: Int = 0, nextId: Int = 0): RecipeStep =
        RecipeStep(
            id = id,
            nextId = nextId,
            typeOfProcess = typeOfProcess,
            parameterProcess1 = parameterProcess1,
            parameterProcess2 = 0,
            priceForTransport = 0,
            transportCellId = 0,
            transportCellReservationId = 0,
            priceForProcess = 0,
            processCellId = 0,
            processCellReservationId = 0,
            timeOfProcess = 0L,
            timeOfTransport = 0L,
            needForTransport = false,
            isTransport = false,
            isProcess = false,
            isStepDone = false
        )
}
