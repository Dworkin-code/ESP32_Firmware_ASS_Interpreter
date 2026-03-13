package com.testbed.nfcrecipetag.ui

import android.content.Intent
import android.os.Bundle
import android.text.Editable
import android.text.TextWatcher
import android.view.LayoutInflater
import android.view.View
import android.widget.AdapterView
import android.widget.ArrayAdapter
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.Spinner
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.testbed.nfcrecipetag.R
import com.testbed.nfcrecipetag.core.tagmodel.DecodedRecipe
import com.testbed.nfcrecipetag.core.tagmodel.ProcessTypes
import com.testbed.nfcrecipetag.core.tagmodel.RawTagDump
import com.testbed.nfcrecipetag.core.tagmodel.RecipeHeader
import com.testbed.nfcrecipetag.core.tagmodel.RecipeStep
import com.testbed.nfcrecipetag.databinding.ActivityEditRecipeBinding

class EditRecipeActivity : AppCompatActivity() {

    private lateinit var binding: ActivityEditRecipeBinding
    private var dump: RawTagDump? = null
    private var decoded: DecodedRecipe? = null
    private var stepViews: List<StepEditorViews> = emptyList()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityEditRecipeBinding.inflate(layoutInflater)
        setContentView(binding.root)
        supportActionBar?.setDisplayHomeAsUpEnabled(true)
        dump = intent.getSerializableExtra(EXTRA_DUMP) as? RawTagDump
        decoded = intent.getSerializableExtra(EXTRA_DECODED) as? DecodedRecipe
        if (dump == null || decoded == null) {
            finish()
            return
        }
        val h = decoded!!.header
        binding.editId.setText(h.id.toString())
        binding.editNumDrinks.setText(h.numOfDrinks.toString())
        binding.editRecipeSteps.setText(h.recipeSteps.toString())
        binding.editBudget.setText(h.actualBudget.toString())
        buildStepEditors()
        binding.editRecipeSteps.addTextChangedListener(object : TextWatcher {
            override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) {}
            override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) {}
            override fun afterTextChanged(s: Editable?) {
                val n = s?.toString()?.toIntOrNull()?.coerceIn(0, 32) ?: return
                if (n != stepViews.size) buildStepEditors()
            }
        })
        binding.btnDoneEdit.setOnClickListener {
            val id = binding.editId.text.toString().toIntOrNull()?.coerceIn(0, 255) ?: h.id
            val numDrinks = binding.editNumDrinks.text.toString().toIntOrNull()?.coerceAtLeast(0) ?: h.numOfDrinks
            val stepsCount = binding.editRecipeSteps.text.toString().toIntOrNull()?.coerceIn(0, 32) ?: h.recipeSteps
            val budget = binding.editBudget.text.toString().toIntOrNull()?.coerceAtLeast(0) ?: h.actualBudget
            val newSteps = buildStepsFromEditors(stepsCount)
            val newHeader = h.copy(
                id = id,
                numOfDrinks = numDrinks,
                recipeSteps = newSteps.size,
                actualBudget = budget
            )
            val edited = decoded!!.copy(header = newHeader, steps = newSteps)
            WriteTagActivity.start(this, dump!!, edited)
            finish()
        }
    }

    private fun buildStepEditors() {
        val container = binding.stepEditorsContainer
        container.removeAllViews()
        val h = decoded!!.header
        val stepsCount = binding.editRecipeSteps.text.toString().toIntOrNull()?.coerceIn(0, 32) ?: h.recipeSteps.coerceIn(0, 32)
        val steps = decoded!!.steps
        val paddedSteps = (0 until stepsCount).map { i ->
            if (i < steps.size) steps[i] else ProcessTypes.createStep(ProcessTypes.SHAKER, 5, i, i)
        }
        val typeNames = ProcessTypes.ALL.map { it.second }
        stepViews = paddedSteps.mapIndexed { index, step ->
            val row = LayoutInflater.from(this).inflate(R.layout.item_step_editor, container, false)
            val label = row.findViewById<TextView>(R.id.step_editor_label)
            val typeSpinner = row.findViewById<Spinner>(R.id.step_editor_type)
            val durationRow = row.findViewById<View>(R.id.step_editor_duration_row)
            val durationEdit = row.findViewById<EditText>(R.id.step_editor_duration)
            label.text = "Step ${index + 1}"
            typeSpinner.adapter = ArrayAdapter(this, android.R.layout.simple_spinner_item, typeNames).apply {
                setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item)
            }
            val typeIndex = ProcessTypes.ALL.indexOfFirst { it.first == step.typeOfProcess }.coerceAtLeast(0)
            typeSpinner.setSelection(typeIndex)
            durationEdit.setText(step.parameterProcess1.coerceIn(0, 255).toString())
            durationRow.visibility = if (step.typeOfProcess == ProcessTypes.SHAKER) View.VISIBLE else View.GONE
            typeSpinner.onItemSelectedListener = object : AdapterView.OnItemSelectedListener {
                override fun onItemSelected(parent: AdapterView<*>?, view: View?, pos: Int, id: Long) {
                    val isShaker = ProcessTypes.ALL[pos].first == ProcessTypes.SHAKER
                    durationRow.visibility = if (isShaker) View.VISIBLE else View.GONE
                }
                override fun onNothingSelected(parent: AdapterView<*>?) {}
            }
            container.addView(row)
            StepEditorViews(typeSpinner, durationEdit)
        }
    }

    private fun buildStepsFromEditors(maxCount: Int): List<RecipeStep> {
        val steps = decoded!!.steps
        return stepViews.take(maxCount).mapIndexed { index, v ->
            val typeIndex = v.typeSpinner.selectedItemPosition.coerceIn(0, ProcessTypes.ALL.lastIndex)
            val typeOfProcess = ProcessTypes.ALL[typeIndex].first
            val param1 = v.durationEdit.text.toString().toIntOrNull()?.coerceIn(0, 255)
                ?: if (typeOfProcess == ProcessTypes.SHAKER) 5 else 0
            val existing = steps.getOrNull(index)
            val base = existing ?: ProcessTypes.createStep(ProcessTypes.SHAKER, 5, index, index)
            base.copy(typeOfProcess = typeOfProcess, parameterProcess1 = param1, id = index, nextId = index)
        }
    }

    private data class StepEditorViews(val typeSpinner: Spinner, val durationEdit: EditText)

    override fun onSupportNavigateUp(): Boolean {
        onBackPressedDispatcher.onBackPressed()
        return true
    }

    companion object {
        private const val EXTRA_DUMP = "dump"
        private const val EXTRA_DECODED = "decoded"
        fun start(activity: AppCompatActivity, dump: RawTagDump, decoded: DecodedRecipe) {
            activity.startActivity(Intent(activity, EditRecipeActivity::class.java)
                .putExtra(EXTRA_DUMP, dump)
                .putExtra(EXTRA_DECODED, decoded))
        }
    }
}
