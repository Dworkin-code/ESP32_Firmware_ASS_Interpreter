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
import android.widget.CheckBox
import android.widget.Spinner
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.testbed.nfcrecipetag.R
import com.testbed.nfcrecipetag.core.cellprofile.getActiveCellProfile
import com.testbed.nfcrecipetag.core.codec.computeStepChecksum
import com.testbed.nfcrecipetag.core.codec.encodeStep
import com.testbed.nfcrecipetag.core.tagmodel.DecodedRecipe
import com.testbed.nfcrecipetag.core.tagmodel.ProcessTypes
import com.testbed.nfcrecipetag.core.tagmodel.RawTagDump
import com.testbed.nfcrecipetag.core.codec.NTAG213_MAX_STEPS
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
        binding.editActualRecipeStep.setText(h.actualRecipeStep.toString())
        binding.editParameters.setText(h.parameters.toString())
        binding.editRecipeDone.isChecked = h.recipeDone
        buildStepEditors()
        updateComputedHeaderDisplay()

        binding.editId.addTextChangedListener(object : TextWatcher {
            override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) {}
            override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) {}
            override fun afterTextChanged(s: Editable?) { updateComputedHeaderDisplay() }
        })
        binding.editRecipeSteps.addTextChangedListener(object : TextWatcher {
            override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) {}
            override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) {}
            override fun afterTextChanged(s: Editable?) {
                val n = s?.toString()?.toIntOrNull()?.coerceIn(0, NTAG213_MAX_STEPS) ?: return
                if (n != stepViews.size) buildStepEditors()
                updateComputedHeaderDisplay()
            }
        })
        binding.btnDoneEdit.setOnClickListener { onDoneEdit() }
    }

    private fun updateComputedHeaderDisplay() {
        val id = binding.editId.text.toString().toIntOrNull()?.coerceIn(0, 255) ?: 0
        binding.rightNumberDisplay.text = (255 - id).toString()
        val stepsCount = binding.editRecipeSteps.text.toString().toIntOrNull()?.coerceIn(0, NTAG213_MAX_STEPS) ?: 0
        val steps = buildStepsFromEditors(stepsCount)
        val stepBytes = steps.flatMap { encodeStep(it).toList() }.toByteArray()
        val checksum = if (stepBytes.isEmpty()) 0 else computeStepChecksum(stepBytes)
        binding.checksumDisplay.text = checksum.toString()
    }

    private fun onDoneEdit() {
        val h = decoded!!.header
        val id = binding.editId.text.toString().toIntOrNull()?.coerceIn(0, 255) ?: h.id
        val numDrinks = binding.editNumDrinks.text.toString().toIntOrNull()?.coerceAtLeast(0) ?: h.numOfDrinks
        val stepsCount = binding.editRecipeSteps.text.toString().toIntOrNull()?.coerceIn(0, NTAG213_MAX_STEPS) ?: h.recipeSteps
        val budget = binding.editBudget.text.toString().toIntOrNull()?.coerceAtLeast(0) ?: h.actualBudget
        val parameters = binding.editParameters.text.toString().toIntOrNull()?.coerceIn(0, 255) ?: h.parameters
        val recipeDone = binding.editRecipeDone.isChecked
        val newSteps = buildStepsFromEditors(stepsCount)

        if (stepsCount != newSteps.size) {
            android.widget.Toast.makeText(
                this,
                "RecipeSteps ($stepsCount) must match the number of steps (${newSteps.size}).",
                android.widget.Toast.LENGTH_LONG
            ).show()
            return
        }
        val maxValidActual = (newSteps.size - 1).coerceAtLeast(0)
        val actualRecipeStep = binding.editActualRecipeStep.text.toString().toIntOrNull()
            ?.coerceIn(0, maxValidActual) ?: h.actualRecipeStep.coerceIn(0, maxValidActual)
        if (actualRecipeStep !in 0 until newSteps.size) {
            android.widget.Toast.makeText(
                this,
                "ActualRecipeStep must be between 0 and ${newSteps.size - 1} (inclusive).",
                android.widget.Toast.LENGTH_LONG
            ).show()
            return
        }

        val newHeader = h.copy(
            id = id,
            numOfDrinks = numDrinks,
            recipeSteps = newSteps.size,
            actualBudget = budget,
            actualRecipeStep = actualRecipeStep,
            parameters = parameters,
            recipeDone = recipeDone
        )
        val edited = decoded!!.copy(header = newHeader, steps = newSteps)
        WriteTagActivity.start(this, dump!!, decoded!!, edited)
        finish()
    }

    private fun buildStepEditors() {
        val profile = getActiveCellProfile()
        val container = binding.stepEditorsContainer
        container.removeAllViews()
        val h = decoded!!.header
        val stepsCount = binding.editRecipeSteps.text.toString().toIntOrNull()?.coerceIn(0, NTAG213_MAX_STEPS) ?: h.recipeSteps.coerceIn(0, NTAG213_MAX_STEPS)
        val steps = decoded!!.steps
        val paddedSteps = (0 until stepsCount).map { i ->
            if (i < steps.size) steps[i] else ProcessTypes.createStep(profile.defaultMaterial, profile.defaultSupportA, i, i)
                .copy(parameterProcess2 = profile.defaultSupportB)
        }
        val typeNames = ProcessTypes.ALL.map { it.second }
        stepViews = paddedSteps.mapIndexed { index, step ->
            val row = LayoutInflater.from(this).inflate(R.layout.item_step_editor, container, false)
            val label = row.findViewById<TextView>(R.id.step_editor_label)
            val typeSpinner = row.findViewById<Spinner>(R.id.step_editor_type)
            val durationRow = row.findViewById<View>(R.id.step_editor_duration_row)
            val durationEdit = row.findViewById<EditText>(R.id.step_editor_duration)
            val supportBEdit = row.findViewById<EditText>(R.id.step_editor_support_b)
            val humanSummary = row.findViewById<TextView>(R.id.step_editor_human_summary)
            val transportCellIdEdit = row.findViewById<EditText>(R.id.step_editor_transport_cell_id)
            val transportCellResIdEdit = row.findViewById<EditText>(R.id.step_editor_transport_cell_res_id)
            val processCellIdEdit = row.findViewById<EditText>(R.id.step_editor_process_cell_id)
            val processCellResIdEdit = row.findViewById<EditText>(R.id.step_editor_process_cell_res_id)
            val priceTransportEdit = row.findViewById<EditText>(R.id.step_editor_price_transport)
            val priceProcessEdit = row.findViewById<EditText>(R.id.step_editor_price_process)
            val needTransportCheck = row.findViewById<CheckBox>(R.id.step_editor_need_transport)
            val isTransportCheck = row.findViewById<CheckBox>(R.id.step_editor_is_transport)
            val isProcessCheck = row.findViewById<CheckBox>(R.id.step_editor_is_process)
            val isDoneCheck = row.findViewById<CheckBox>(R.id.step_editor_is_done)
            label.text = "Step ${index + 1}"
            typeSpinner.adapter = ArrayAdapter(this, android.R.layout.simple_spinner_item, typeNames).apply {
                setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item)
            }
            val typeIndex = ProcessTypes.ALL.indexOfFirst { it.first == step.typeOfProcess }.coerceAtLeast(0)
            typeSpinner.setSelection(typeIndex)
            durationEdit.setText(step.parameterProcess1.coerceIn(profile.supportARange).toString())
            supportBEdit.setText(step.parameterProcess2.coerceIn(profile.supportBRange).toString())
            transportCellIdEdit.setText(step.transportCellId.toString())
            transportCellResIdEdit.setText(step.transportCellReservationId.toString())
            processCellIdEdit.setText(step.processCellId.toString())
            processCellResIdEdit.setText(step.processCellReservationId.toString())
            priceTransportEdit.setText(step.priceForTransport.toString())
            priceProcessEdit.setText(step.priceForProcess.toString())
            needTransportCheck.isChecked = step.needForTransport
            isTransportCheck.isChecked = step.isTransport
            isProcessCheck.isChecked = step.isProcess
            isDoneCheck.isChecked = step.isStepDone

            updateHumanSummary(humanSummary, step.typeOfProcess, step.parameterProcess1, step.parameterProcess2)
            durationRow.visibility = if (step.typeOfProcess == ProcessTypes.SHAKER) View.VISIBLE else View.GONE
            typeSpinner.onItemSelectedListener = object : AdapterView.OnItemSelectedListener {
                override fun onItemSelected(parent: AdapterView<*>?, view: View?, pos: Int, id: Long) {
                    val typeValue = ProcessTypes.ALL[pos].first
                    val isShaker = typeValue == ProcessTypes.SHAKER
                    durationRow.visibility = if (isShaker) View.VISIBLE else View.GONE
                    val param1 = durationEdit.text.toString().toIntOrNull() ?: step.parameterProcess1
                    val param2 = supportBEdit.text.toString().toIntOrNull() ?: step.parameterProcess2
                    updateHumanSummary(humanSummary, typeValue, param1, param2)
                }
                override fun onNothingSelected(parent: AdapterView<*>?) {}
            }

            fun attachParamWatcher(edit: EditText) {
                edit.addTextChangedListener(object : TextWatcher {
                    override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) {}
                    override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) {}
                    override fun afterTextChanged(s: Editable?) {
                        val typeValue = ProcessTypes.ALL[typeSpinner.selectedItemPosition.coerceIn(0, ProcessTypes.ALL.lastIndex)].first
                        val p1 = durationEdit.text.toString().toIntOrNull() ?: 0
                        val p2 = supportBEdit.text.toString().toIntOrNull() ?: 0
                        updateHumanSummary(humanSummary, typeValue, p1, p2)
                    }
                })
            }
            attachParamWatcher(durationEdit)
            attachParamWatcher(supportBEdit)
            container.addView(row)
            StepEditorViews(
                typeSpinner = typeSpinner,
                durationEdit = durationEdit,
                supportBEdit = supportBEdit,
                transportCellIdEdit = transportCellIdEdit,
                transportCellResIdEdit = transportCellResIdEdit,
                processCellIdEdit = processCellIdEdit,
                processCellResIdEdit = processCellResIdEdit,
                priceTransportEdit = priceTransportEdit,
                priceProcessEdit = priceProcessEdit,
                needTransportCheck = needTransportCheck,
                isTransportCheck = isTransportCheck,
                isProcessCheck = isProcessCheck,
                isDoneCheck = isDoneCheck
            )
        }
        updateComputedHeaderDisplay()
    }

    /**
     * Builds step list from editor rows. id and nextId are preserved from the decoded
     * recipe for existing steps (so recipe flow is not broken). For newly added steps
     * we use a linear chain: id=index, nextId=index+1 (or index for last step).
     */
    private fun buildStepsFromEditors(maxCount: Int): List<RecipeStep> {
        val profile = getActiveCellProfile()
        val steps = decoded!!.steps
        return stepViews.take(maxCount).mapIndexed { index, v ->
            val typeIndex = v.typeSpinner.selectedItemPosition.coerceIn(0, ProcessTypes.ALL.lastIndex)
            val typeOfProcess = ProcessTypes.ALL[typeIndex].first
            val param1 = v.durationEdit.text.toString().toIntOrNull()?.coerceIn(profile.supportARange)
                ?: profile.defaultSupportA
            val param2 = v.supportBEdit.text.toString().toIntOrNull()?.coerceIn(profile.supportBRange) ?: profile.defaultSupportB
            val existing = steps.getOrNull(index)
            val (stepId, stepNextId) = if (existing != null) {
                existing.id to existing.nextId
            } else {
                val nextId = if (index < maxCount - 1) index + 1 else index
                index to nextId
            }
            val base = existing ?: ProcessTypes.createStep(profile.defaultMaterial, profile.defaultSupportA, stepId, stepNextId)
                .copy(parameterProcess2 = profile.defaultSupportB)
            base.copy(
                typeOfProcess = typeOfProcess,
                parameterProcess1 = param1,
                parameterProcess2 = param2,
                id = stepId,
                nextId = stepNextId,
                priceForTransport = v.priceTransportEdit.text.toString().toIntOrNull() ?: base.priceForTransport,
                transportCellId = v.transportCellIdEdit.text.toString().toIntOrNull() ?: base.transportCellId,
                transportCellReservationId = v.transportCellResIdEdit.text.toString().toIntOrNull() ?: base.transportCellReservationId,
                priceForProcess = v.priceProcessEdit.text.toString().toIntOrNull() ?: base.priceForProcess,
                processCellId = v.processCellIdEdit.text.toString().toIntOrNull() ?: base.processCellId,
                processCellReservationId = v.processCellResIdEdit.text.toString().toIntOrNull() ?: base.processCellReservationId,
                needForTransport = v.needTransportCheck.isChecked,
                isTransport = v.isTransportCheck.isChecked,
                isProcess = v.isProcessCheck.isChecked,
                isStepDone = v.isDoneCheck.isChecked
            )
        }
    }

    private data class StepEditorViews(
        val typeSpinner: Spinner,
        val durationEdit: EditText,
        val supportBEdit: EditText,
        val transportCellIdEdit: EditText,
        val transportCellResIdEdit: EditText,
        val processCellIdEdit: EditText,
        val processCellResIdEdit: EditText,
        val priceTransportEdit: EditText,
        val priceProcessEdit: EditText,
        val needTransportCheck: CheckBox,
        val isTransportCheck: CheckBox,
        val isProcessCheck: CheckBox,
        val isDoneCheck: CheckBox
    )

    private fun updateHumanSummary(target: TextView, typeOfProcess: Int, param1: Int, param2: Int) {
        val typeName = ProcessTypes.name(typeOfProcess)
        val human = when (typeOfProcess) {
            ProcessTypes.SHAKER -> "$typeName ($typeOfProcess): duration=$param1 s (raw1=$param1, raw2=$param2)"
            ProcessTypes.STORAGE_ALCOHOL,
            ProcessTypes.STORAGE_NON_ALCOHOL -> {
                val drink = ProcessTypes.drinkName(typeOfProcess, param1) ?: "drink#$param1"
                "$typeName ($typeOfProcess): $drink, $param2 ml (param1=$param1, param2=$param2)"
            }
            else -> "$typeName ($typeOfProcess): param1=$param1, param2=$param2"
        }
        target.text = human
    }

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
