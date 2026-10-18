// Placeholder for builds without the trained picker (NOTA_NEURAL=OFF). The network is never run in such a build
// (Engine checks kNeuralBuilt), so these are empty.
#include "PickerWeights.h"

namespace nota
{
const float kPickerWInp[1] = {};
const float kPickerBInp[1] = {};
const float kPickerWIh[1] = {};
const float kPickerWHh[1] = {};
const float kPickerBIh[1] = {};
const float kPickerBHh[1] = {};
const float kPickerWOut[1] = {};
const float kPickerBOut[1] = {};
} // namespace nota
