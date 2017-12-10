//============================================================================================================
//!
//! \file Seq-G2a.cpp
//!
//! \brief Seq-G2a is a thing.
//!
//============================================================================================================


#include "Gratrix.hpp"
#include "dsp/digital.hpp"


namespace GTX {
namespace Seq_G2a {


#define PROGRAMS  12
#define RATIO     2
#define NOB_ROWS  2
#define NOB_COLS  16
#define BUT_ROWS  6
#define BUT_COLS  (NOB_COLS*RATIO)
#define OUT_LEFT  1
#define OUT_RIGHT 1

#define GATE_STATES 4


struct Impl : Module {
	enum ParamIds {
		CLOCK_PARAM,
		RUN_PARAM,
		RESET_PARAM,
		STEPS_PARAM,
		PROG_PARAM,
		PLAY_PARAM,
		EDIT_PARAM,
		SPAN_R_PARAM,
		SPAN_C_PARAM,
		CLEAR_PARAM,
		RANDOM_PARAM,
		COPY_PARAM,
		PASTE_PARAM,
		NOB_PARAM,
		BUT_PARAM  = NOB_PARAM + (NOB_COLS * NOB_ROWS),
		NUM_PARAMS = BUT_PARAM + (BUT_COLS * BUT_ROWS)
	};
	enum InputIds {
		CLOCK_INPUT,
		EXT_CLOCK_INPUT,
		RESET_INPUT,
		STEPS_INPUT,
		PROG_INPUT,
		GATE_INPUT,      // N+1
		VOCT_INPUT,      // N+1
		NUM_INPUTS,
		OFF_INPUTS = GATE_INPUT
	};
	enum OutputIds {
		NOB_OUTPUT,
		BUT_OUTPUT  = NOB_OUTPUT + NOB_ROWS * (OUT_LEFT + OUT_RIGHT),
		GATE_OUTPUT = BUT_OUTPUT + BUT_ROWS * (OUT_LEFT + OUT_RIGHT),  // N
		VOCT_OUTPUT,                                                   // N
		NUM_OUTPUTS,
		OFF_OUTPUTS = GATE_OUTPUT
	};
	enum LightIds {
		RUNNING_LIGHT,
		RESET_LIGHT,
		PROG_LIGHT,
		CLEAR_LIGHT = PROG_LIGHT + PROGRAMS * 2,
		RANDOM_LIGHT,
		COPY_LIGHT,
		PASTE_LIGHT,
		BUT_LIGHT,
		NUM_LIGHTS = BUT_LIGHT   + (BUT_COLS * BUT_ROWS) * 3
	};

	struct Decode
	{
		/*static constexpr*/ float e = static_cast<float>(PROGRAMS);  // Static constexpr gives
		/*static constexpr*/ float s = 1.0f / e;                      // link error on Mac build.

		float in    = 0;
		float out   = 0;
		int   note  = 0;
		int   key   = 0;
		int   oct   = 0;

		void step(float input)
		{
			int safe, fnote;

			in    = input;
			fnote = std::floor(in * PROGRAMS + 0.5f);
			out   = fnote * s;
			note  = static_cast<int>(fnote);
			safe  = note + (PROGRAMS * 1000);  // push away from negative numbers
			key   = safe % PROGRAMS;
			oct   = (safe / PROGRAMS) - 1000;
		}
	};

	static constexpr bool is_nob_snap(std::size_t row) { return true; }

	static constexpr std::size_t nob_val_map(std::size_t row, std::size_t col)     { return NOB_OUTPUT + (OUT_LEFT + OUT_RIGHT) * row + col; }
	static constexpr std::size_t but_val_map(std::size_t row, std::size_t col)     { return BUT_OUTPUT + (OUT_LEFT + OUT_RIGHT) * row + col; }

	static constexpr std::size_t nob_map(std::size_t row, std::size_t col)                  { return NOB_PARAM  +      NOB_COLS * row + col; }
	static constexpr std::size_t but_map(std::size_t row, std::size_t col)                  { return BUT_PARAM  +      BUT_COLS * row + col; }
	static constexpr std::size_t led_map(std::size_t row, std::size_t col, std::size_t idx) { return BUT_LIGHT  + 3 * (BUT_COLS * row + col) + idx; }

	static constexpr std::size_t imap(std::size_t port, std::size_t bank)
	{
		return (port < OFF_INPUTS)  ? port : port + bank * (NUM_INPUTS  - OFF_INPUTS);
	}

	static constexpr std::size_t omap(std::size_t port, std::size_t bank)
	{
		return (port < OFF_OUTPUTS) ? port : port + bank * (NUM_OUTPUTS - OFF_OUTPUTS);
	}


	Widget *widget;
	Decode prg_nob;
	Decode prg_cv;
	bool running = true;
	SchmittTrigger clockTrigger; // for external clock
	// For buttons
	SchmittTrigger runningTrigger;
	SchmittTrigger resetTrigger;
	SchmittTrigger clearTrigger;
	SchmittTrigger randomTrigger;
	SchmittTrigger copyTrigger;
	SchmittTrigger pasteTrigger;
	SchmittTrigger gateTriggers[BUT_ROWS][BUT_COLS];
	float phase = 0.0;
	int index = 0;
	int numSteps = 0;
	std::size_t play_prog = 0;
	std::size_t edit_prog = 0;
	std::size_t prev_prog = 0;
	bool        masterState = false;

	uint8_t knobState[PROGRAMS][NOB_ROWS][NOB_COLS] = {};
	uint8_t gateState[PROGRAMS][BUT_ROWS][BUT_COLS] = {};
	uint8_t knobCopy[NOB_ROWS][NOB_COLS] = {};
	uint8_t gateCopy[BUT_ROWS][BUT_COLS] = {};

	float resetLight = 0.0;
	float clearLight = 0.0;
	float randomLight = 0.0;
	float copyLight = 0.0;
	float pasteLight = 0.0;
	float stepLights[BUT_ROWS][BUT_COLS] = {};

	enum GateMode
	{
		GM_OFF,
		GM_CONTINUOUS,
		GM_RETRIGGER,
		GM_TRIGGER,
	};

	PulseGenerator gatePulse;

	//--------------------------------------------------------------------------------------------------------
	//! \brief Constructor.

	Impl(Widget *widget_)
	:
		Module(
			NUM_PARAMS,
			(GTX__N+1) * (NUM_INPUTS  - OFF_INPUTS ) + OFF_INPUTS,
			(GTX__N  ) * (NUM_OUTPUTS - OFF_OUTPUTS) + OFF_OUTPUTS,
			NUM_LIGHTS),
		widget(widget_)
	{
		reset();
	}

	//--------------------------------------------------------------------------------------------------------
	//! \brief Step function.

	void step() override
	{
		const float lightLambda = 0.075;

		// Decode program info

		prg_nob.step(params[PROG_PARAM].value / 12.0f);
		prg_cv .step(inputs[PROG_INPUT].value);

		// Input leds

		float prog_leds[PROGRAMS * 2]  = {};
		prog_leds[prg_nob.key * 2    ] = 1.0f;  // Green
		prog_leds[prg_cv .key * 2 + 1] = 1.0f;  // Red

		// Determine what is playing and what is editing

		bool play_is_cv = (params[PLAY_PARAM].value < 0.5f);
		bool edit_is_cv = (params[EDIT_PARAM].value < 0.5f);

		play_prog = play_is_cv ? prg_cv.key : prg_nob.key;
		edit_prog = edit_is_cv ? prg_cv.key : prg_nob.key;

		// Run

		if (runningTrigger.process(params[RUN_PARAM].value))
		{
			running = !running;
		}
		lights[RUNNING_LIGHT].value = running ? 1.0 : 0.0;

		bool nextStep = false;

		if (running)
		{
			if (inputs[EXT_CLOCK_INPUT].active)
			{
				// External clock
				if (clockTrigger.process(inputs[EXT_CLOCK_INPUT].value))
				{
					phase = 0.0;
					nextStep = true;
				}
			}
			else
			{
				// Internal clock
				float clockTime = powf(2.0, params[CLOCK_PARAM].value + inputs[CLOCK_INPUT].value);
				phase += clockTime / engineGetSampleRate();

				if (phase >= 1.0)
				{
					phase -= 1.0;
					nextStep = true;
				}
			}
		}

		// Update knobs BUGGY

		if (masterState)
		{
			knob_push(edit_prog);
			masterState = false;
		}
		else if (prev_prog == edit_prog)
		{
			knob_pull(edit_prog);
		}
		else
		{
			knob_pull(prev_prog);
			knob_push(edit_prog);
		}

		// Trigger buttons

		{
			const float dim = 1.0f / lightLambda / engineGetSampleRate();

			// Reset
			if (resetTrigger.process(params[RESET_PARAM].value + inputs[RESET_INPUT].value))
			{
				phase = 0.0;
				index = BUT_COLS;
				nextStep = true;
				resetLight = 1.0;
			}
			resetLight -= resetLight * dim;

			// Clear current program
			if (clearTrigger.process(params[CLEAR_PARAM].value))
			{
				clear_prog(edit_prog);
				clearLight = 1.0;
			}
			clearLight -= clearLight * dim;

			// Randomise current program
			if (randomTrigger.process(params[RANDOM_PARAM].value))
			{
				randomize_prog(edit_prog);
				randomLight = 1.0;
			}
			randomLight -= randomLight * dim;

			// Copy current program
			if (copyTrigger.process(params[COPY_PARAM].value))
			{
				copy_prog(edit_prog);
				copyLight = 1.0;
			}
			copyLight -= copyLight * dim;

			// Paste current program
			if (pasteTrigger.process(params[PASTE_PARAM].value))
			{
				paste_prog(edit_prog);
				pasteLight = 1.0;
			}
			pasteLight -= pasteLight * dim;
		}

		numSteps = RATIO * clampi(roundf(params[STEPS_PARAM].value + inputs[STEPS_INPUT].value), 1, NOB_COLS);

		if (nextStep)
		{
			// Advance step
			index += 1;

			if (index >= numSteps)
			{
				index = 0;
			}

			for (int row = 0; row < BUT_ROWS; row++)
			{
				stepLights[row][index] = 1.0;
			}

			gatePulse.trigger(1e-3);
		}

		bool pulse = gatePulse.process(1.0 / engineGetSampleRate());

		// Gate buttons

		for (int col = 0; col < BUT_COLS; ++col)
		{
			for (int row = 0; row < BUT_ROWS; ++row)
			{
				// User input to alter state of buttons

				if (gateTriggers[row][col].process(params[but_map(row, col)].value))
				{
					auto state = gateState[edit_prog][row][col];

					if (++state >= GATE_STATES)
					{
						state = GM_OFF;
					}

					std::size_t span_r = static_cast<std::size_t>(params[SPAN_R_PARAM].value + 0.5);
					std::size_t span_c = static_cast<std::size_t>(params[SPAN_C_PARAM].value + 0.5);

					for (std::size_t r = row; r < row + span_r && r < BUT_ROWS; ++r)
					{
						for (std::size_t c = col; c < col + span_c && c < BUT_COLS; ++c)
						{
							gateState[edit_prog][r][c] = state;
						}
					}
				}

				// Get state of buttons for lights

				{
					bool gateOn = (running && (col == index) && (gateState[edit_prog][row][col] > 0));

					switch (gateState[edit_prog][row][col])
					{
						case GM_CONTINUOUS :                            break;
						case GM_RETRIGGER  : gateOn = gateOn && !pulse; break;
						case GM_TRIGGER    : gateOn = gateOn &&  pulse; break;
						default            : break;
					}

					stepLights[row][col] -= stepLights[row][col] / lightLambda / engineGetSampleRate();

					if (col < numSteps)
					{
						float val = (play_prog == edit_prog) ? 1.0 : 0.1;

						lights[led_map(row, col, 1)].value = gateState[edit_prog][row][col] == GM_CONTINUOUS ? 1.0 - val * stepLights[row][col] : val * stepLights[row][col];  // Green
						lights[led_map(row, col, 2)].value = gateState[edit_prog][row][col] == GM_RETRIGGER  ? 1.0 - val * stepLights[row][col] : val * stepLights[row][col];  // Blue
						lights[led_map(row, col, 0)].value = gateState[edit_prog][row][col] == GM_TRIGGER    ? 1.0 - val * stepLights[row][col] : val * stepLights[row][col];  // Red
					}
					else
					{
						lights[led_map(row, col, 1)].value = 0.01;  // Green
						lights[led_map(row, col, 2)].value = 0.01;  // Blue
						lights[led_map(row, col, 0)].value = 0.01;  // Red
					}
				}
			}
		}

		// Compute row output

		int nob_index = index / RATIO;

		float nob_val[NOB_ROWS];
		for (std::size_t row = 0; row < NOB_ROWS; ++row)
		{
			nob_val[row] = knobState[play_prog][row][nob_index];

			if (is_nob_snap(row)) nob_val[row] /= 12.0f;
		}

		bool but_val[BUT_ROWS];
		for (std::size_t row = 0; row < BUT_ROWS; ++row)
		{
			but_val[row] = running && (gateState[play_prog][row][index] > 0);

			switch (gateState[play_prog][row][index])
			{
				case GM_CONTINUOUS :                                        break;
				case GM_RETRIGGER  : but_val[row] = but_val[row] && !pulse; break;
				case GM_TRIGGER    : but_val[row] = but_val[row] &&  pulse; break;
				default            : break;
			}
		}

		// Write row outputs

		for (std::size_t row = 0; row < NOB_ROWS; ++row)
		{
			if (OUT_LEFT || OUT_RIGHT) outputs[nob_val_map(row, 0)].value = nob_val[row];
			if (OUT_LEFT && OUT_RIGHT) outputs[nob_val_map(row, 1)].value = nob_val[row];
		}

		for (std::size_t row = 0; row < BUT_ROWS; ++row)
		{
			if (OUT_LEFT || OUT_RIGHT) outputs[but_val_map(row, 0)].value = but_val[row] ? 10.0f : 0.0f;
			if (OUT_LEFT && OUT_RIGHT) outputs[but_val_map(row, 1)].value = but_val[row] ? 10.0f : 0.0f;
		}

		// Detemine ploy outputs

		for (std::size_t i=0; i<GTX__N && i<BUT_ROWS; ++i)
		{
			// Pass V/OCT trough (for now)
			outputs[omap(VOCT_OUTPUT, i)].value = inputs[imap(VOCT_INPUT, i)].active ? inputs[imap(VOCT_INPUT, i)].value : inputs[imap(VOCT_INPUT, GTX__N)].value;

			// Generate gate out
			float gate_in  = inputs[imap(GATE_INPUT, i)].active ? inputs[imap(GATE_INPUT, i)].value : inputs[imap(GATE_INPUT, GTX__N)].value;

			outputs[omap(GATE_OUTPUT, i)].value = (but_val[i] && gate_in >= 1.0f) ? 10.0f : 0.0f;
		}

		// Update LEDs

		lights[RESET_LIGHT] .value = resetLight;
		lights[CLEAR_LIGHT] .value = clearLight;
		lights[RANDOM_LIGHT].value = randomLight;
		lights[COPY_LIGHT]  .value = copyLight;
		lights[PASTE_LIGHT] .value = pasteLight;

		for (std::size_t i=0; i<PROGRAMS; ++i)
		{
			lights[PROG_LIGHT + i * 2    ].value = prog_leds[i * 2    ];
			lights[PROG_LIGHT + i * 2 + 1].value = prog_leds[i * 2 + 1];
		}

		prev_prog = edit_prog;
	}

	//--------------------------------------------------------------------------------------------------------
	//! \brief Save state.

	json_t *toJson() override
	{
		if (json_t *rootJ = json_object())
		{
			// Running
			json_object_set_new(rootJ, "running", json_boolean(running));

			// Knobs
			if (json_t *ja = json_array())
			{
				for (std::size_t prog = 0; prog < PROGRAMS; ++prog)
				{
					for (std::size_t row = 0; row < NOB_ROWS; ++row)
					{
						for (std::size_t col = 0; col < NOB_COLS; ++col)
						{
							if (json_t *ji = json_integer(static_cast<int>(knobState[prog][row][col])))
							{
								json_array_append_new(ja, ji);
							}
						}
					}
				}

				json_object_set_new(rootJ, "knobs", ja);
			}

			// Gates
			if (json_t *ja = json_array())
			{
				for (std::size_t prog = 0; prog < PROGRAMS; ++prog)
				{
					for (std::size_t row = 0; row < BUT_ROWS; ++row)
					{
						for (std::size_t col = 0; col < BUT_COLS; ++col)
						{
							if (json_t *ji = json_integer(static_cast<int>(gateState[prog][row][col])))
							{
								json_array_append_new(ja, ji);
							}
						}
					}
				}

				json_object_set_new(rootJ, "gates", ja);
			}

			return rootJ;
		}

		return nullptr;
	}

	//--------------------------------------------------------------------------------------------------------
	//! \brief Load state.

	void fromJson(json_t *rootJ) override
	{
		// Running
		if (json_t *jb = json_object_get(rootJ, "running"))
		{
			running = json_is_true(jb);
		}

		// Knobs
		if (json_t *ja = json_object_get(rootJ, "knobs"))
		{
			for (std::size_t i = 0, prog = 0; prog < PROGRAMS; ++prog)
			{
				for (std::size_t row = 0; row < NOB_ROWS; ++row)
				{
					for (std::size_t col = 0; col < NOB_COLS; ++col, ++i)
					{
						if (json_t *ji = json_array_get(ja, i))
						{
							//! \todo Range check
							knobState[prog][row][col] = json_integer_value(ji);
						}
					}
				}
			}
		}

		// Gates
		if (json_t *ja = json_object_get(rootJ, "gates"))
		{
			for (std::size_t i = 0, prog = 0; prog < PROGRAMS; ++prog)
			{
				for (std::size_t row = 0; row < BUT_ROWS; ++row)
				{
					for (std::size_t col = 0; col < BUT_COLS; ++col, ++i)
					{
						if (json_t *ji = json_array_get(ja, i))
						{
							int value = json_integer_value(ji);

							if (value < 0 || value >= GATE_STATES)
							{
								gateState[prog][row][col] = GM_OFF;
							}
							else
							{
								gateState[prog][row][col] = static_cast<uint8_t>(value);
							}
						}
					}
				}
			}
		}

		masterState = true;
	}

	//--------------------------------------------------------------------------------------------------------
	//! \brief Reset state.

	void reset() override
	{
		for (std::size_t prog = 0; prog < PROGRAMS; ++prog)
		{
			for (std::size_t col = 0; col < BUT_COLS; col++)
			{
				for (std::size_t row = 0; row < BUT_ROWS; row++)
				{
					gateState[prog][row][col] = GM_OFF;
				}
			}
		}
	}

	//--------------------------------------------------------------------------------------------------------
	//! \brief Random state.

	void randomize() override
	{
		for (std::size_t prog = 0; prog < PROGRAMS; ++prog)
		{
			for (std::size_t col = 0; col < BUT_COLS; col++)
			{
				for (std::size_t row = 0; row < BUT_ROWS; row++)
				{
					uint32_t r = randomu32() % (GATE_STATES + 1);

					if (r >= GATE_STATES) r = GM_CONTINUOUS;

					gateState[prog][row][col] = r;
				}
			}
		}
	}

	//--------------------------------------------------------------------------------------------------------
	//! \brief Knob params to state.

	void knob_pull(std::size_t prog)
	{
		for (std::size_t col = 0; col < NOB_COLS; col++)
		{
			for (std::size_t row = 0; row < NOB_ROWS; ++row)
			{
				knobState[prog][row][col] = params[nob_map(row, col)].value;
			}
		}
	}

	//--------------------------------------------------------------------------------------------------------
	//! \brief Knob state to params.

	void knob_push(std::size_t prog)
	{
		for (std::size_t col = 0; col < NOB_COLS; col++)
		{
			for (std::size_t row = 0; row < NOB_ROWS; ++row)
			{
				widget->params[nob_map(row, col)]->setValue(knobState[prog][row][col]);
			}
		}
	}

	//--------------------------------------------------------------------------------------------------------
	//! \brief Clear a program.

	void clear_prog(std::size_t prog)
	{
		for (std::size_t col = 0; col < NOB_COLS; col++)
		{
			for (std::size_t row = 0; row < NOB_ROWS; ++row)
			{
				knobState[prog][row][col] = 0;

				widget->params[nob_map(row, col)]->setValue(0);
			}
		}

		for (std::size_t col = 0; col < BUT_COLS; col++)
		{
			for (std::size_t row = 0; row < BUT_ROWS; row++)
			{
				gateState[prog][row][col] = GM_OFF;
			}
		}
	}

	//--------------------------------------------------------------------------------------------------------
	//! \brief Randomize a program.

	void randomize_prog(std::size_t prog)
	{
		for (std::size_t col = 0; col < BUT_COLS; col++)
		{
			for (std::size_t row = 0; row < BUT_ROWS; row++)
			{
				uint32_t r = randomu32() % (GATE_STATES + 1);

				if (r >= GATE_STATES) r = GM_CONTINUOUS;

				gateState[prog][row][col] = r;
			}
		}
	}

	//--------------------------------------------------------------------------------------------------------
	//! \brief Copy a program.

	void copy_prog(std::size_t prog)
	{
		for (std::size_t col = 0; col < NOB_COLS; col++)
		{
			for (std::size_t row = 0; row < NOB_ROWS; ++row)
			{
				knobCopy[row][col] = knobState[prog][row][col];
			}
		}

		for (std::size_t col = 0; col < BUT_COLS; col++)
		{
			for (std::size_t row = 0; row < BUT_ROWS; row++)
			{
				gateCopy[row][col] = gateState[prog][row][col];
			}
		}
	}

	//--------------------------------------------------------------------------------------------------------
	//! \brief Paste a program.

	void paste_prog(std::size_t prog)
	{
		for (std::size_t col = 0; col < NOB_COLS; col++)
		{
			for (std::size_t row = 0; row < NOB_ROWS; ++row)
			{
				knobState[prog][row][col] = knobCopy[row][col];

				widget->params[nob_map(row, col)]->setValue(knobCopy[row][col]);
			}
		}

		for (std::size_t col = 0; col < BUT_COLS; col++)
		{
			for (std::size_t row = 0; row < BUT_ROWS; row++)
			{
				gateState[prog][row][col] = gateCopy[row][col];
			}
		}
	}
};


//============================================================================================================
//! \brief The Widget.

Widget::Widget()
{
	Impl *module = new Impl(this);
	setModule(module);
	box.size = Vec((OUT_LEFT+NOB_COLS+OUT_RIGHT)*3*15, 380);

	float grid_left  = 3*15*OUT_LEFT;
	float grid_right = 3*15*OUT_RIGHT;
	float grid_size  = box.size.x - grid_left - grid_right;

	float g_nobX[NOB_COLS] = {};
	for (std::size_t i = 0; i < NOB_COLS; i++)
	{
		float x  = grid_size / static_cast<double>(NOB_COLS);
		g_nobX[i] = grid_left + x * (i + 0.5);
	}

	float g_butX[BUT_COLS] = {};
	for (std::size_t i = 0; i < BUT_COLS; i++)
	{
		float x  = grid_size / static_cast<double>(BUT_COLS);
		g_butX[i] = grid_left + x * (i + 0.5);
	}

	float gridXl =              grid_left  / 2;
	float gridXr = box.size.x - grid_right / 2;

	float portX[10] = {};
	for (std::size_t i = 0; i < 10; i++)
	{
		float x = 5*6*15 / static_cast<double>(10);
		portX[i] = 2*6*15 + x * (i + 0.5);
	}
	float dX = 0.5*(portX[1]-portX[0]);

	float portY[4] = {};
	portY[0] = gy(2-0.24);
	portY[1] = gy(2+0.22);
	float dY = 0.5*(portY[1]-portY[0]);
	portY[2] = portY[0] + 0.45 * dY;
	portY[3] = portY[0] +        dY;

	float gridY[NOB_ROWS + BUT_ROWS] = {};
	{
		std::size_t j = 0;
		int pos = 35;

		for (std::size_t row = 0; row < NOB_ROWS; ++row, ++j)
		{
			pos += rad_n_s() + 4.5;
			gridY[j] = pos;
			pos += rad_n_s() + 4.5;
		}

		for (std::size_t row = 0; row < BUT_ROWS; ++row, ++j)
		{
			pos += rad_but() + 3.5;
			gridY[j] = pos;
			pos += rad_but() + 3.5;
		}
	}

	#if GTX__SAVE_SVG
	{
		PanelGen pg(assetPlugin(plugin, "build/res/Seq-G2a.svg"), box.size, "SEQ-G2a");

		for (std::size_t i=0; i<NOB_COLS-1; i++)
		{
			double x  = 0.5 * (g_nobX[i] + g_nobX[i+1]);
			double y0 = gridY[0];
			double y1 = gridY[NOB_ROWS + BUT_ROWS - 1];

			if (i % 4 == 3)
				pg.line(Vec(x, y0), Vec(x, y1), "fill:none;stroke:#7092BE;stroke-width:3");
			else
				pg.line(Vec(x, y0), Vec(x, y1), "fill:none;stroke:#7092BE;stroke-width:1");
		}

		pg.line(Vec(portX[0],    portY[0]), Vec(portX[0],    portY[1]), "fill:none;stroke:#7092BE;stroke-width:1");
		pg.line(Vec(portX[2],    portY[0]), Vec(portX[2],    portY[1]), "fill:none;stroke:#7092BE;stroke-width:1");
		pg.line(Vec(portX[3],    portY[0]), Vec(portX[3],    portY[1]), "fill:none;stroke:#7092BE;stroke-width:1");
		pg.line(Vec(portX[4],    portY[0]), Vec(portX[4],    portY[1]), "fill:none;stroke:#7092BE;stroke-width:1");
		pg.line(Vec(portX[8],    portY[0]), Vec(portX[9],    portY[0]), "fill:none;stroke:#7092BE;stroke-width:1");
		pg.line(Vec(portX[8],    portY[1]), Vec(portX[9],    portY[1]), "fill:none;stroke:#7092BE;stroke-width:1");
		pg.line(Vec(portX[4]+dX, portY[2]), Vec(portX[6],    portY[2]), "fill:none;stroke:#7092BE;stroke-width:1");
		pg.line(Vec(portX[4]+dX, portY[3]), Vec(portX[4]+dX, portY[2]), "fill:none;stroke:#7092BE;stroke-width:1");
		pg.line(Vec(portX[4],    portY[3]), Vec(portX[4]+dX, portY[3]), "fill:none;stroke:#7092BE;stroke-width:1");

		pg.line(Vec(portX[0]-dX, portY[0]-29), Vec(portX[0]-dX, portY[1]+16), "fill:none;stroke:#7092BE;stroke-width:2");
		pg.line(Vec(portX[3]+dX, portY[0]-29), Vec(portX[3]+dX, portY[1]+16), "fill:none;stroke:#7092BE;stroke-width:2");
		pg.line(Vec(portX[6]+dX, portY[0]-29), Vec(portX[6]+dX, portY[1]+16), "fill:none;stroke:#7092BE;stroke-width:2");
		pg.line(Vec(portX[9]+dX, portY[0]-29), Vec(portX[9]+dX, portY[1]+16), "fill:none;stroke:#7092BE;stroke-width:2");

		pg.nob_sml_raw(portX[0], portY[0], "CLOCK");
		pg.nob_sml_raw(portX[1], portY[0], "RUN");
		pg.nob_sml_raw(portX[2], portY[0], "RESET");
		pg.nob_sml_raw(portX[3], portY[0], "STEPS");
		pg.nob_sml_raw(portX[4], portY[0], "PROG");
		pg.nob_sml_raw(portX[5], portY[0], "PLAY");
		pg.nob_sml_raw(portX[6], portY[0], "EDIT");
		pg.nob_sml_raw(portX[7], portY[0], "ROWS");
		pg.nob_sml_raw(portX[8], portY[0], "CLEAR");
		pg.nob_sml_raw(portX[9], portY[0], "RAND");

		pg.nob_sml_raw(portX[1], portY[1], "EXT CLK");
		pg.nob_sml_raw(portX[7], portY[1], "COLS");
		pg.nob_sml_raw(portX[8], portY[1], "COPY");
		pg.nob_sml_raw(portX[9], portY[1], "PASTE");

		pg.tog_raw2   (portX[5], portY[2], "KNOB", "CV");
		pg.tog_raw2   (portX[6], portY[2], "KNOB", "CV");

		pg.bus_in (0, 2, "GATE");
		pg.bus_in (1, 2, "V/OCT");
		pg.bus_out(8, 2, "GATE");
		pg.bus_out(7, 2, "V/OCT");
	}
	#endif

	{
		SVGPanel *panel = new SVGPanel();
		panel->box.size = box.size;
		panel->setBackground(SVG::load(assetPlugin(plugin, "res/Seq-G2a.svg")));
		addChild(panel);
	}

	addChild(createScrew<ScrewSilver>(Vec(15, 0)));
	addChild(createScrew<ScrewSilver>(Vec(box.size.x-30, 0)));
	addChild(createScrew<ScrewSilver>(Vec(15, 365)));
	addChild(createScrew<ScrewSilver>(Vec(box.size.x-30, 365)));

	addParam(createParam<RoundSmallBlackKnob>    (n_s(portX[0], portY[0]), module, Impl::CLOCK_PARAM, -2.0, 6.0, 2.0));
	addParam(createParam<LEDButton>              (but(portX[1], portY[0]), module, Impl::RUN_PARAM, 0.0, 1.0, 0.0));
	addChild(createLight<MediumLight<GreenLight>>(l_m(portX[1], portY[0]), module, Impl::RUNNING_LIGHT));
	addParam(createParam<LEDButton>              (but(portX[2], portY[0]), module, Impl::RESET_PARAM, 0.0, 1.0, 0.0));
	addChild(createLight<MediumLight<GreenLight>>(l_m(portX[2], portY[0]), module, Impl::RESET_LIGHT));
	addParam(createParam<RoundSmallBlackSnapKnob>(n_s(portX[3], portY[0]), module, Impl::STEPS_PARAM, 1.0, NOB_COLS, NOB_COLS));
	addParam(createParam<RoundSmallBlackSnapKnob>(n_s(portX[4], portY[0]), module, Impl::PROG_PARAM, 0.0, 11.0, 0.0));
	addParam(createParam<CKSS>                   (tog(portX[5], portY[2]), module, Impl::PLAY_PARAM, 0.0, 1.0, 1.0));
	addParam(createParam<CKSS>                   (tog(portX[6], portY[2]), module, Impl::EDIT_PARAM, 0.0, 1.0, 1.0));

	addChild(createLight<SmallLight<GreenRedLight>>(l_s(portX[5] + dX - 30, portY[1] + 5 + 1), module, Impl::PROG_LIGHT +  0*2));  // C
	addChild(createLight<SmallLight<GreenRedLight>>(l_s(portX[5] + dX - 25, portY[1] - 5 + 1), module, Impl::PROG_LIGHT +  1*2));  // C#
	addChild(createLight<SmallLight<GreenRedLight>>(l_s(portX[5] + dX - 20, portY[1] + 5 + 1), module, Impl::PROG_LIGHT +  2*2));  // D
	addChild(createLight<SmallLight<GreenRedLight>>(l_s(portX[5] + dX - 15, portY[1] - 5 + 1), module, Impl::PROG_LIGHT +  3*2));  // Eb
	addChild(createLight<SmallLight<GreenRedLight>>(l_s(portX[5] + dX - 10, portY[1] + 5 + 1), module, Impl::PROG_LIGHT +  4*2));  // E
	addChild(createLight<SmallLight<GreenRedLight>>(l_s(portX[5] + dX     , portY[1] + 5 + 1), module, Impl::PROG_LIGHT +  5*2));  // F
	addChild(createLight<SmallLight<GreenRedLight>>(l_s(portX[5] + dX +  5, portY[1] - 5 + 1), module, Impl::PROG_LIGHT +  6*2));  // Fs
	addChild(createLight<SmallLight<GreenRedLight>>(l_s(portX[5] + dX + 10, portY[1] + 5 + 1), module, Impl::PROG_LIGHT +  7*2));  // G
	addChild(createLight<SmallLight<GreenRedLight>>(l_s(portX[5] + dX + 15, portY[1] - 5 + 1), module, Impl::PROG_LIGHT +  8*2));  // Ab
	addChild(createLight<SmallLight<GreenRedLight>>(l_s(portX[5] + dX + 20, portY[1] + 5 + 1), module, Impl::PROG_LIGHT +  9*2));  // A
	addChild(createLight<SmallLight<GreenRedLight>>(l_s(portX[5] + dX + 25, portY[1] - 5 + 1), module, Impl::PROG_LIGHT + 10*2));  // Bb
	addChild(createLight<SmallLight<GreenRedLight>>(l_s(portX[5] + dX + 30, portY[1] + 5 + 1), module, Impl::PROG_LIGHT + 11*2));  // B

	addParam(createParam<RoundSmallBlackSnapKnob>(n_s(portX[7], portY[0]), module, Impl::SPAN_R_PARAM, 1.0, 8.0, 1.0));
	addParam(createParam<RoundSmallBlackSnapKnob>(n_s(portX[7], portY[1]), module, Impl::SPAN_C_PARAM, 1.0, 8.0, 1.0));
	addParam(createParam<LEDButton>              (but(portX[8], portY[0]), module, Impl::CLEAR_PARAM, 0.0, 1.0, 0.0));
	addChild(createLight<MediumLight<GreenLight>>(l_m(portX[8], portY[0]), module, Impl::CLEAR_LIGHT));
	addParam(createParam<LEDButton>              (but(portX[9], portY[0]), module, Impl::RANDOM_PARAM, 0.0, 1.0, 0.0));
	addChild(createLight<MediumLight<GreenLight>>(l_m(portX[9], portY[0]), module, Impl::RANDOM_LIGHT));
	addParam(createParam<LEDButton>              (but(portX[8], portY[1]), module, Impl::COPY_PARAM, 0.0, 1.0, 0.0));
	addChild(createLight<MediumLight<GreenLight>>(l_m(portX[8], portY[1]), module, Impl::COPY_LIGHT));
	addParam(createParam<LEDButton>              (but(portX[9], portY[1]), module, Impl::PASTE_PARAM, 0.0, 1.0, 0.0));
	addChild(createLight<MediumLight<GreenLight>>(l_m(portX[9], portY[1]), module, Impl::PASTE_LIGHT));

	addInput(createInput<PJ301MPort>(prt(portX[0], portY[1]), module, Impl::CLOCK_INPUT));
	addInput(createInput<PJ301MPort>(prt(portX[1], portY[1]), module, Impl::EXT_CLOCK_INPUT));
	addInput(createInput<PJ301MPort>(prt(portX[2], portY[1]), module, Impl::RESET_INPUT));
	addInput(createInput<PJ301MPort>(prt(portX[3], portY[1]), module, Impl::STEPS_INPUT));
	addInput(createInput<PJ301MPort>(prt(portX[4], portY[1]), module, Impl::PROG_INPUT));

	{
		std::size_t j = 0;

		for (std::size_t row = 0; row < NOB_ROWS; ++row, ++j)
		{
			if (OUT_LEFT ) addOutput(createOutput<PJ301MPort>(prt(gridXl, gridY[j]), module, Impl::nob_val_map(row, 0)));
			if (OUT_RIGHT) addOutput(createOutput<PJ301MPort>(prt(gridXr, gridY[j]), module, Impl::nob_val_map(row, 1)));
		}

		for (std::size_t row = 0; row < BUT_ROWS; ++row, ++j)
		{
			if (OUT_LEFT ) addOutput(createOutput<PJ301MPort>(prt(gridXl, gridY[j]), module, Impl::but_val_map(row, 0)));
			if (OUT_RIGHT) addOutput(createOutput<PJ301MPort>(prt(gridXr, gridY[j]), module, Impl::but_val_map(row, 1)));
		}
	}

	{
		std::size_t j = 0;

		for (std::size_t row = 0; row < NOB_ROWS; ++row, ++j)
		{
			for (std::size_t col = 0; col < NOB_COLS; ++col)
			{
				if (Impl::is_nob_snap(row))
				{
					addParam(createParam<RoundSmallBlackSnapKnob>(n_s(g_nobX[col], gridY[j]), module, Impl::nob_map(row, col), 0.0, 12.0, 0.0));
				}
				else
				{
					addParam(createParam<RoundSmallBlackKnob>    (n_s(g_nobX[col], gridY[j]), module, Impl::nob_map(row, col), 0.0, 10.0, 0.0));
				}
			}
		}

		for (std::size_t row = 0; row < BUT_ROWS; ++row, ++j)
		{
			for (std::size_t col = 0; col < BUT_COLS; ++col)
			{
				addParam(createParam<LEDButton>                     (but(g_butX[col], gridY[j]), module, Impl::but_map(row, col), 0.0, 1.0, 0.0));
				addChild(createLight<MediumLight<RedGreenBlueLight>>(l_m(g_butX[col], gridY[j]), module, Impl::led_map(row, col, 0)));
			}
		}
	}

	for (std::size_t i=0; i<GTX__N; ++i)
	{
		addInput(createInput<PJ301MPort>  (prt(px(0, i), py(2, i)), module, Impl::imap(Impl::GATE_INPUT, i)));
		addInput(createInput<PJ301MPort>  (prt(px(1, i), py(2, i)), module, Impl::imap(Impl::VOCT_INPUT, i)));

		addOutput(createOutput<PJ301MPort>(prt(px(8, i), py(2, i)), module, Impl::omap(Impl::GATE_OUTPUT, i)));
		addOutput(createOutput<PJ301MPort>(prt(px(7, i), py(2, i)), module, Impl::omap(Impl::VOCT_OUTPUT, i)));
	}

	addInput(createInput<PJ301MPort>(prt(gx(0), gy(2)), module, Impl::imap(Impl::GATE_INPUT, GTX__N)));
	addInput(createInput<PJ301MPort>(prt(gx(1), gy(2)), module, Impl::imap(Impl::VOCT_INPUT, GTX__N)));
}


} // Seq_G2a
} // GTX