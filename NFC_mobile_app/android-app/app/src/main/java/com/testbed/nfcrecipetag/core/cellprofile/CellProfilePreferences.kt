package com.testbed.nfcrecipetag.core.cellprofile

import android.content.Context
import android.content.SharedPreferences

private const val PREFS_NAME = "settings"
private const val KEY_ACTIVE_CELL_PROFILE_ID = "active_cell_profile_id"

/**
 * Persists and retrieves the active cell profile ID. Uses the same SharedPreferences
 * as the rest of the app ("settings").
 */
fun Context.getActiveCellProfile(): CellProfile {
    val prefs = getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
    val id = prefs.getString(KEY_ACTIVE_CELL_PROFILE_ID, CellProfileRegistry.DEFAULT_PROFILE_ID)
    return CellProfileRegistry.getByIdOrDefault(id)
}

fun Context.getActiveCellProfileId(): String {
    val prefs = getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
    return prefs.getString(KEY_ACTIVE_CELL_PROFILE_ID, CellProfileRegistry.DEFAULT_PROFILE_ID)
        ?: CellProfileRegistry.DEFAULT_PROFILE_ID
}

fun Context.setActiveCellProfileId(profileId: String) {
    getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        .edit()
        .putString(KEY_ACTIVE_CELL_PROFILE_ID, profileId)
        .apply()
}
