// does nothing but look at its outputs (like the core does for each voice's track): measures the cost of a wide node itself
class Noop extends AudioWorkletProcessor { process(inputs, outputs) { return true; } }
registerProcessor('noop', Noop);
