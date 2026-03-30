package com.testbed.nfcrecipetag.core.debug

import android.content.Context
import android.util.Log
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

object DebugLog {
    private const val PREF_NAME = "debug_log"
    private const val KEY_LOG = "log"
    private const val TAG = "DebugLog"
    private const val MAX_LOG_CHARS = 120_000
    private val dateFormat = SimpleDateFormat("yyyy-MM-dd HH:mm:ss.SSS", Locale.US)

    private fun timestamp(): String = dateFormat.format(Date())

    /** Appends a line with timestamp. */
    fun append(context: Context, line: String) {
        append(context, line, null)
    }

    /** Appends a line with timestamp; if [throwable] is non-null, appends exception class, message and stack trace. */
    fun append(context: Context, line: String, throwable: Throwable?) {
        try {
            val prefs = context.getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE)
            var existing = prefs.getString(KEY_LOG, "") ?: ""
            val entry = buildString {
                append("[").append(timestamp()).append("] ").appendLine(line)
                throwable?.let { t ->
                    append("  Exception: ").append(t.javaClass.name).append(": ").append(t.message ?: "").appendLine()
                    t.stackTrace.take(25).forEach { ste ->
                        append("    at ").append(ste.className).append(".").append(ste.methodName)
                        append("(").append(ste.fileName ?: "?").append(":").append(ste.lineNumber).append(")").appendLine()
                    }
                    if (t.stackTrace.size > 25) append("    ... ${t.stackTrace.size - 25} more").appendLine()
                }
            }
            existing += entry
            if (existing.length > MAX_LOG_CHARS) {
                existing = existing.drop(existing.length - MAX_LOG_CHARS)
                val firstNewline = existing.indexOf('\n')
                if (firstNewline > 0) existing = existing.drop(firstNewline + 1)
            }
            prefs.edit().putString(KEY_LOG, existing).apply()
        } catch (e: Exception) {
            Log.e(TAG, "Failed to append debug log", e)
        }
    }

    /** Appends a section header (no exception). */
    fun appendSection(context: Context, title: String) {
        append(context, "========== $title ==========")
    }

    fun getAll(context: Context): String {
        return try {
            val prefs = context.getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE)
            prefs.getString(KEY_LOG, "") ?: ""
        } catch (_: Exception) {
            ""
        }
    }

    fun clear(context: Context) {
        try {
            val prefs = context.getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE)
            prefs.edit().remove(KEY_LOG).apply()
        } catch (e: Exception) {
            Log.e(TAG, "Failed to clear debug log", e)
        }
    }
}
